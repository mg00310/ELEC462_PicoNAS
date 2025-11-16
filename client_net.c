#include "client.h"

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
    struct FileInfo item = args->file_info; // Use the copied FileInfo
    char current_path[MAX_PATH];
    strcpy(current_path, args->curr_path);
    free(args); // Free the args struct immediately after copying data

    char buffer[4096]; char resp[5] = {0};
    int64_t file_size_net, file_size;
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(g_server_ip);
    serv_addr.sin_port = htons(PORT);
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 연결", item.filename); return NULL;
    }
    snprintf(buffer, sizeof(buffer), "%s %s %s", CMD_AUTH, g_user, g_pass);
    if (write(sock, buffer, strlen(buffer)) <= 0) { close(sock); return NULL; }
    if (read_full(sock, resp, 4) != 0) { close(sock); return NULL; }
    if (strncmp(resp, RESP_OK, 4) != 0) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 인증", item.filename);
        close(sock); return NULL;
    }
    uint32_t net_len, len;
    read_full(sock, &net_len, sizeof(uint32_t)); len = ntohl(net_len); read_full(sock, buffer, len);
    read_full(sock, &net_len, sizeof(uint32_t)); len = ntohl(net_len); read_full(sock, buffer, len);
    
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s/%s", current_path, item.filename);
    snprintf(buffer, sizeof(buffer), "%s %s", CMD_GET, full_path);

    if (write(sock, buffer, strlen(buffer)) <= 0) { close(sock); return NULL; }
    if (read_full(sock, resp, 4) != 0 || strncmp(resp, RESP_GET_S, 4) != 0) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 파일없음", item.filename);
        close(sock); return NULL;
    }
    if (read_full(sock, &file_size_net, sizeof(int64_t)) != 0) { close(sock); return NULL; }
    file_size = be64toh(file_size_net);
    int fd = open(item.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 파일생성", item.filename);
        close(sock); return NULL;
    }
    ssize_t bytes_read;
    int64_t total_received = 0;
    while (total_received < file_size)
    {
        int64_t remaining = file_size - total_received;
        size_t to_read = sizeof(buffer);
        if (remaining < to_read) {
            to_read = (size_t)remaining;
        }

        bytes_read = read(sock, buffer, to_read);
        if (bytes_read <= 0) { // 연결 끊김 또는 오류
            break;
        }

        ssize_t written_bytes = write(fd, buffer, bytes_read);
        if (written_bytes != bytes_read) { // 쓰기 오류
            break;
        }

        total_received += bytes_read;
        snprintf(g_status_msg, 100, "다운중(%.20s...): %" PRId64 "/%" PRId64 " KB", 
                 item.filename, total_received / 1024, file_size / 1024);
    }
    close(fd); close(sock);
    if (total_received == file_size) {
        snprintf(g_status_msg, 100, "다운완료: %.30s...", item.filename);
        add_queue(item.filename);
    } else {
        snprintf(g_status_msg, 100, "다운실패(%.30s...): 불완전", item.filename);
        remove(item.filename);
    }
    return NULL;
}

void start_downloads() {
    int selected_count = 0;
    for (int i = 0; i < g_file_count; i++) {
        if (g_file_list[i].is_selected && g_file_list[i].type != 'd') {
            g_file_list[i].is_selected = 0;

            struct DownloadArgs* args = malloc(sizeof(struct DownloadArgs));
            if (!args) {
                snprintf(g_status_msg, 100, "메모리 할당 오류");
                continue;
            }
            args->file_info = g_file_list[i]; // 구조체 복사
            strcpy(args->curr_path, g_current_path);

            pthread_t tid;
            if (pthread_create(&tid, NULL, download_thread, (void*)args) != 0) {
                perror("pthread_create() 에러");
                free(args);
            } else {
                pthread_detach(tid);
                selected_count++;
            }
        }
    }
    if (selected_count == 0) snprintf(g_status_msg, 100, "선택된 파일이 없습니다!");
    else snprintf(g_status_msg, 100, "%d개 파일 다운로드 시작...", selected_count);
}
