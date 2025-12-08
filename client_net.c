#include "client.h"
#include <unistd.h>
#include <errno.h>
#include <byteswap.h>

#include <endian.h>
#ifndef htobe64
#define htobe64(x) __builtin_bswap64(x)
#endif
#ifndef be64toh
#define be64toh(x) __builtin_bswap64(x)
#endif

void* download_dir_thread(void* arg);

// --- 프로토콜 (서버 통신) ---
int auth_client(int sock) {
    char user[MAX_NAME], pass[MAX_NAME], buffer[MAX_PATH * 2], resp[5] = {0};
    uint32_t net_len, len;
    printf("사용자명: ");
    fgets(user, MAX_NAME, stdin); user[strcspn(user, "\n")] = 0;
    printf("비밀번호: ");
    get_password(pass, MAX_NAME);
    strcpy(g_user, user); strcpy(g_pass, pass);
    snprintf(buffer, sizeof(buffer), "%s %s %s", CMD_AUTH, user, pass);
    if (write(sock, buffer, strlen(buffer)) <= 0) return 0;
    if (read_full(sock, resp, 4) != 0) return 0;
    if (strncmp(resp, RESP_OK, 4) == 0) {
        if (read_full(sock, &net_len, sizeof(uint32_t)) != 0) return 0;
        len = ntohl(net_len);
        if (read_full(sock, g_root_path, len) != 0) return 0;
        g_root_path[len] = 0;
        if (read_full(sock, &net_len, sizeof(uint32_t)) != 0) return 0;
        len = ntohl(net_len);
        if (read_full(sock, g_current_path, len) != 0) return 0;
        g_current_path[len] = 0;
        return 1;
    }
    return 0;
}

void upload_file(const char* localfile, const char* servername){
    int sock = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PORT);
    serv.sin_addr.s_addr = inet_addr(g_server_ip);

    connect(sock,(struct sockaddr*)&serv,sizeof(serv));

    // 인증
    char buf[2048], resp[5]={0};
    sprintf(buf,"%s %s %s", CMD_AUTH, g_user, g_pass);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    // 파일 열기
    int fd=open(localfile,O_RDONLY);
    if(fd<0){ snprintf(g_status_msg,100,"파일 없음"); return; }

    sprintf(buf,"%s %s", CMD_PUT, servername);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    if(strncmp(resp,RESP_PUT_S,4)!=0){
        snprintf(g_status_msg,100,"서버 업로드 거부");
        close(fd);close(sock);return;
    }

    // 크기 전송
    off_t size=lseek(fd,0,SEEK_END);
    lseek(fd,0,SEEK_SET);
    int64_t netsize=htobe64(size);
    write(sock,&netsize,8);

    // 파일 전송
    ssize_t r; char filebuf[4096];
    while((r=read(fd,filebuf,4096))>0) write(sock,filebuf,r);

    read(sock,resp,4);
    if(strncmp(resp,RESP_PUT_E,4)==0)
        snprintf(g_status_msg,100,"업로드 완료!");
    else
        snprintf(g_status_msg,100,"업로드 실패!");

    close(fd); close(sock);
}

void request_list(int sock) {
    char resp[5] = {0};
    uint32_t net_file_count;
    if (g_file_list) free(g_file_list);
    g_file_list = NULL;
    g_file_count = 0; g_selected_item = 0; g_scroll_offset = 0;
    g_focus_zone = ZONE_LIST;
    if (write(sock, CMD_LS, 4) <= 0) handle_error("write(LS) 에러");
    if (read_full(sock, resp, 4) != 0) return;
    if (strncmp(resp, RESP_LS_S, 4) != 0) {
        snprintf(g_status_msg, 100, "LS 응답 수신 실패"); return;
    }
    if (read_full(sock, &net_file_count, sizeof(uint32_t)) != 0) return;
    g_file_count = ntohl(net_file_count);
    if (g_file_count == 0) {
        read_full(sock, resp, 4);
        return;
    }
    g_file_list = malloc(g_file_count * sizeof(struct FileInfo));
    if (g_file_list == NULL) handle_error("malloc() 에러");
    ssize_t target_size = g_file_count * sizeof(struct FileInfo);
    if (read_full(sock, g_file_list, target_size) != 0) handle_error("read(structs) 에러");
    read_full(sock, resp, 4); 
    for (int i = 0; i < g_file_count; i++) {
        g_file_list[i].is_selected = 0;
        g_file_list[i].is_downloaded = 0;
    }
    sort_list();
}

void cd_client(int sock, const char* dirname) {
    char buffer[MAX_PATH * 2], resp[5] = {0};
    uint32_t net_len, len;
    snprintf(buffer, sizeof(buffer), "%s %s", CMD_CD, dirname);
    if (write(sock, buffer, strlen(buffer)) <= 0) return;
    if (read_full(sock, resp, 4) != 0) return;
    if (strncmp(resp, RESP_OK, 4) == 0) {
        if (read_full(sock, &net_len, sizeof(uint32_t)) != 0) return;
        len = ntohl(net_len);
        if (read_full(sock, g_current_path, len) != 0) return;
        g_current_path[len] = 0;
        request_list(sock);
    } else {
        snprintf(g_status_msg, 100, "디렉터리 이동 불가.");
    }
}

// --- 다운로드 로직 ---
void* download_thread(void* arg) {
    struct DownloadArgs* args = (struct DownloadArgs*)arg;
    struct FileInfo item = args->file_info;
    char current_path[MAX_PATH];
    strcpy(current_path, args->curr_path);
    free(args);

    // 1. 다운로드 진행률을 추적할 빈 슬롯을 찾아서 할당
    int slot_index = -1;
    pthread_mutex_lock(&g_prog_mutex);
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (!g_down_prog[i].active) {
            g_down_prog[i].active = 1;
            strncpy(g_down_prog[i].filename, item.filename, MAX_FILENAME - 1);
            g_down_prog[i].filename[MAX_FILENAME - 1] = '\0';
            g_down_prog[i].progress = 0.0;
            slot_index = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_prog_mutex);

    if (slot_index == -1) {
        snprintf(g_status_msg, 100, "최대 동시 다운로드 수 초과");
        return NULL;
    }

    char buffer[4096]; char resp[5] = {0};
    int64_t file_size;
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(g_server_ip);
    serv_addr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 연결", item.filename);
        goto cleanup;
    }

    snprintf(buffer, sizeof(buffer), "%s %s %s", CMD_AUTH, g_user, g_pass);
    if (write(sock, buffer, strlen(buffer)) <= 0) { close(sock); goto cleanup; }
    if (read_full(sock, resp, 4) != 0 || strncmp(resp, RESP_OK, 4) != 0) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 인증", item.filename);
        close(sock); goto cleanup;
    }
    uint32_t net_len, len;
    read_full(sock, &net_len, sizeof(uint32_t)); len = ntohl(net_len); read_full(sock, buffer, len);
    read_full(sock, &net_len, sizeof(uint32_t)); len = ntohl(net_len); read_full(sock, buffer, len);
    
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s/%s", current_path, item.filename);
    snprintf(buffer, sizeof(buffer), "%s %s", CMD_GET, full_path);

    if (write(sock, buffer, strlen(buffer)) <= 0) { close(sock); goto cleanup; }
    if (read_full(sock, resp, 4) != 0 || strncmp(resp, RESP_GET_S, 4) != 0) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 파일없음", item.filename);
        close(sock); goto cleanup;
    }

    uint64_t file_size_net;
    if (read_full(sock, &file_size_net, sizeof(uint64_t)) != 0) { close(sock); goto cleanup; }
    file_size = be64toh(file_size_net);

    int fd = open(item.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 파일생성", item.filename);
        close(sock); goto cleanup;
    }

    ssize_t bytes_read;
    int64_t total_received = 0;
    double last_prog = 0.0;

    while (total_received < file_size) {
        int64_t remaining = file_size - total_received;
        size_t to_read = sizeof(buffer);
        if (remaining < to_read) to_read = (size_t)remaining;

        bytes_read = read(sock, buffer, to_read);
        if (bytes_read <= 0) break;

        ssize_t written_bytes = write(fd, buffer, bytes_read);
        if (written_bytes != bytes_read) break;

        total_received += bytes_read;
        
        // 2. 다운로드 진행률을 계산하고 UI 스레드와 공유
        if (file_size > 0) {
            double down_prog = (double)total_received / file_size;
            // 약 1% 이상 변경될 때마다 UI 갱신을 위해 값 업데이트
            if (down_prog - last_prog >= 0.01 || total_received == file_size) {
                pthread_mutex_lock(&g_prog_mutex);
                g_down_prog[slot_index].progress = down_prog;
                pthread_mutex_unlock(&g_prog_mutex);
                last_prog = down_prog;
                snprintf(g_status_msg, 100, "다운중(%.20s...): %.0f%%", item.filename, down_prog * 100);
            }
        }
    }
    
    close(fd); close(sock);

    if (total_received == file_size) {
        snprintf(g_status_msg, 100, "다운완료: %.30s...", item.filename);
        add_queue(item.filename);
    } else {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 불완전", item.filename);
        remove(item.filename);
    }

cleanup:
    // 3. 다운로드 완료/실패 시, 사용했던 진행률 추적 슬롯을 반납
    if (slot_index != -1) {
        pthread_mutex_lock(&g_prog_mutex);
        g_down_prog[slot_index].active = 0;
        pthread_mutex_unlock(&g_prog_mutex);
    }
    return NULL;
}

void start_downloads() {
    int selected_count = 0;

    for (int i = 0; i < g_file_count; i++) {

        if (!g_file_list[i].is_selected)
            continue;

        struct DownloadArgs* args = malloc(sizeof(struct DownloadArgs));
        if (!args) {
            snprintf(g_status_msg, 100, "메모리 할당 오류");
            continue;
        }

        args->file_info = g_file_list[i];
        strcpy(args->curr_path, g_current_path);

        pthread_t tid;

        if (g_file_list[i].type == 'd') {
            if (pthread_create(&tid, NULL, download_dir_thread, args) != 0) {
                perror("pthread_create() 에러");
                free(args);
            } else {
                pthread_detach(tid);
                selected_count++;
            }
        } else {
            if (pthread_create(&tid, NULL, download_thread, args) != 0) {
                perror("pthread_create() 에러");
                free(args);
            } else {
                pthread_detach(tid);
                selected_count++;
            }
        }

        g_file_list[i].is_selected = 0;
    }

    if (selected_count == 0)
        snprintf(g_status_msg, 100, "선택된 항목이 없습니다!");
    else
        snprintf(g_status_msg, 100, "%d개 다운로드 시작...", selected_count);
}

void* download_dir_thread(void* arg) {
    struct DownloadArgs* args = (struct DownloadArgs*)arg;
    
    // args->file_info에 서버의 디렉터리 정보가 담겨 있습니다.
    char buffer[4096], resp[5] = {0};
    char full_path[MAX_PATH];
    
    // 인증 및 경로 설정 후 서버에 요청할 전체 경로 생성
    // (예: /users/jm/data/mydir)
    snprintf(full_path, sizeof(full_path),
              "%s/%s", args->curr_path, args->file_info.filename);

    // 서버 연결
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(g_server_ip);
    serv_addr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        snprintf(g_status_msg, 100, "폴더 다운로드 실패(연결)");
        free(args);
        return NULL;
    }

    // 인증 시도
    snprintf(buffer, sizeof(buffer), "%s %s %s", CMD_AUTH, g_user, g_pass);
    write(sock, buffer, strlen(buffer));

    // 인증 응답 수신
    if (read_full(sock, resp, 4) != 0 || strncmp(resp, RESP_OK, 4) != 0) {
        snprintf(g_status_msg, 100, "폴더 다운로드 실패(인증)");
        close(sock);
        free(args);
        return NULL;
    }

    // 서버의 루트 경로 및 현재 경로 길이/내용 무시
    uint32_t net_len, len;
    read_full(sock, &net_len, 4); len = ntohl(net_len); read_full(sock, buffer, len);
    read_full(sock, &net_len, 4); len = ntohl(net_len); read_full(sock, buffer, len);


    // GETDIR 요청 전송
    snprintf(buffer, sizeof(buffer), "%s %s", CMD_GETDIR, full_path);
    write(sock, buffer, strlen(buffer));

    // 서버 응답 확인
    read_full(sock, resp, 4);
    if (strncmp(resp, RESP_GETDIR_S, 4) != 0) {
        snprintf(g_status_msg, 100, "폴더 다운로드 실패(응답)");
        close(sock);
        free(args);
        return NULL;
    }

    // 파일 크기 수신
    uint64_t size_net;
    read_full(sock, &size_net, sizeof(size_net));
    uint64_t total_size = be64toh(size_net);

    // 저장 이름 (폴더 이름.tar)
    char save_name[MAX_FILENAME + 5]; // +5 for ".tar\0"
    snprintf(save_name, sizeof(save_name), "%s.tar", args->file_info.filename);

    // 로컬 저장 경로 구성 (다운로드 디렉터리/폴더 이름.tar)
    char savepath[MAX_PATH];
    // ******* 앞서의 'item' undeclared 오류를 해결한 수정된 부분 *******
    snprintf(savepath, sizeof(savepath), "%s/%s", g_download_dir, save_name);
    // ************************************************************
    
    int fd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(g_status_msg, 100, "파일 저장 실패: %s", strerror(errno));
        close(sock);
        free(args);
        return NULL;
    }


    // 본문 (tar 파일) 다운로드
    uint64_t received = 0;
    while (received < total_size) {
        ssize_t r = read(sock, buffer, sizeof(buffer));
        if (r <= 0) break;
        
        ssize_t w = write(fd, buffer, r);
        if (w <= 0) break; // 쓰기 실패
        
        received += w;
    }

    close(fd);
    close(sock);
    free(args);
    
    if (received == total_size) {
        snprintf(g_status_msg, 100, "폴더 다운로드 완료: %s", save_name);
    } else {
         snprintf(g_status_msg, 100, "폴더 다운로드 실패 (전송 불완전)");
    }
    
    return NULL;
}