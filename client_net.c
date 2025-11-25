#define _GNU_SOURCE
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
    if (log_socket_write(sock, buffer, strlen(buffer)) <= 0) return 0;
    if (log_socket_read(sock, resp, 4) != 0) return 0;
    if (strncmp(resp, RESP_OK, 4) == 0) {
        if (log_socket_read(sock, &net_len, sizeof(uint32_t)) != 0) return 0;
        len = ntohl(net_len);
        if (log_socket_read(sock, g_root_path, len) != 0) return 0;
        g_root_path[len] = 0;
        if (log_socket_read(sock, &net_len, sizeof(uint32_t)) != 0) return 0;
        len = ntohl(net_len);
        if (log_socket_read(sock, g_current_path, len) != 0) return 0;
        g_current_path[len] = 0;
        return 1;
    }
    return 0;
}

void request_list(int sock) {
    client_log(LOG_DEBUG, "CMD: LS - listing files in current directory");
    char resp[5] = {0};
    uint32_t net_file_count;
    if (g_file_list) free(g_file_list);
    g_file_list = NULL;
    g_file_count = 0; g_selected_item = 0; g_scroll_offset = 0;
    g_focus_zone = ZONE_LIST;
    if (log_socket_write(sock, CMD_LS, 4) <= 0) {
        client_log(LOG_ERROR, "write(LS) failed: %s", strerror(errno));
        handle_error("write(LS) 에러");
    }
    if (log_socket_read(sock, resp, 4) != 0) {
        client_log(LOG_ERROR, "read_full(LS_S) failed");
        return;
    }
    if (strncmp(resp, RESP_LS_S, 4) != 0) {
        client_log(LOG_WARN, "Invalid response for LS_S: %s", resp);
        snprintf(g_status_msg, 100, "LS 응답 수신 실패"); return;
    }
    if (log_socket_read(sock, &net_file_count, sizeof(uint32_t)) != 0) {
        client_log(LOG_ERROR, "read_full(file_count) failed");
        return;
    }
    g_file_count = ntohl(net_file_count);
    client_log(LOG_INFO, "LS received %d files", g_file_count);
    if (g_file_count == 0) {
        log_socket_read(sock, resp, 4);
        return;
    }
    g_file_list = malloc(g_file_count * sizeof(struct FileInfo));
    if (g_file_list == NULL) {
        client_log(LOG_FATAL, "malloc for file list failed");
        handle_error("malloc() 에러");
    }
    ssize_t target_size = g_file_count * sizeof(struct FileInfo);
    if (log_socket_read(sock, g_file_list, target_size) != 0) {
        client_log(LOG_ERROR, "read_full(file_list structs) failed");
        handle_error("read(structs) 에러");
    }
    log_socket_read(sock, resp, 4); // LS_E
    for (int i = 0; i < g_file_count; i++) {
        g_file_list[i].is_selected = 0;
        g_file_list[i].is_downloaded = 0;
    }
    sort_list();
}

void cd_client(int sock, const char* dirname) {
    client_log(LOG_INFO, "CMD: CD to '%s'", dirname);
    char buffer[MAX_PATH * 2], resp[5] = {0};
    uint32_t net_len, len;
    snprintf(buffer, sizeof(buffer), "%s %s", CMD_CD, dirname);
    if (log_socket_write(sock, buffer, strlen(buffer)) <= 0) {
        client_log(LOG_ERROR, "write(CD) failed: %s", strerror(errno));
        return;
    }
    if (log_socket_read(sock, resp, 4) != 0) {
        client_log(LOG_ERROR, "read_full(CD response) failed");
        return;
    }


    if (strncmp(resp, RESP_OK, 4) == 0) {
        if (log_socket_read(sock, &net_len, sizeof(uint32_t)) != 0) return;
        len = ntohl(net_len);
        if (log_socket_read(sock, g_current_path, len) != 0) return;
        g_current_path[len] = 0;
        client_log(LOG_INFO, "CD successful, new path: %s", g_current_path);
        request_list(sock);
    } else {
        client_log(LOG_WARN, "CD failed by server for dir '%s'", dirname);
        snprintf(g_status_msg, 100, "디렉터리 이동 불가.");
    }
}

char* cat_client_fetch(int sock, const char* filename, size_t* content_size) {
    client_log(LOG_INFO, "CMD: CAT for file '%s'", filename);
    char cmd_buffer[MAX_PATH * 2];
    char resp[5] = {0};
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s %s", CMD_CAT, filename);

    if (log_socket_write(sock, cmd_buffer, strlen(cmd_buffer)) <= 0) {
        client_log(LOG_ERROR, "write(CAT) failed: %s", strerror(errno));
        if (content_size) *content_size = 0;
        return NULL;
    }

    if (log_socket_read(sock, resp, 4) != 0) {
        client_log(LOG_ERROR, "read_full(CAT response) failed");
        if (content_size) *content_size = 0;
        return NULL;
    }



    if (strncmp(resp, RESP_OK, 4) != 0) {
        client_log(LOG_WARN, "CAT failed by server for file '%s'", filename);
        if (content_size) *content_size = 0;
        return NULL;
    }

    client_log(LOG_DEBUG, "CAT for '%s' starting.", filename);
    // Allocate initial buffer
    size_t current_capacity = 4096; // Initial buffer size
    char* content_buffer = (char*)malloc(current_capacity);
    if (!content_buffer) {
        client_log(LOG_FATAL, "malloc for CAT buffer failed");
        if (content_size) *content_size = 0;
        return NULL;
    }
    size_t current_length = 0;

    char data_chunk[4096];
    ssize_t bytes_read;

    while (1) {
        // Use the raw 'read' here, not debug_read, to avoid spamming the debug panel
        bytes_read = read(sock, data_chunk, sizeof(data_chunk));

        if (bytes_read <= 0) {
            client_log(LOG_ERROR, "read() during CAT failed or connection closed prematurely. Bytes read: %ld", bytes_read);
            free(content_buffer);
            if (content_size) *content_size = 0;
            return NULL;
        }

        // Append new data first
        if (current_length + bytes_read + 1 > current_capacity) {
            size_t new_capacity = current_capacity * 2;
            if (new_capacity < current_length + bytes_read + 1) {
                new_capacity = current_length + bytes_read + 1;
            }
            char* temp_buffer = (char*)realloc(content_buffer, new_capacity);
            if (!temp_buffer) {
                client_log(LOG_FATAL, "realloc for CAT buffer failed on chunk append");
                free(content_buffer);
                if (content_size) *content_size = 0;
                return NULL;
            }
            content_buffer = temp_buffer;
            current_capacity = new_capacity;
        }
        memcpy(content_buffer + current_length, data_chunk, bytes_read);
        current_length += bytes_read;

        // Now, search for the end marker in the combined buffer
        // To optimize, we only need to search in the area where the marker could possibly be (near the end of the previous data and the new chunk)
        char* search_start = content_buffer;
        if (current_length > sizeof(data_chunk) + 4) {
             search_start = content_buffer + current_length - sizeof(data_chunk) - 4;
        }
       
        char* end_marker_pos = memmem(search_start, content_buffer + current_length - search_start, RESP_CAT_E, 4);

        if (end_marker_pos != NULL) {
            size_t final_len = end_marker_pos - content_buffer;
            
            char* final_buffer = (char*)realloc(content_buffer, final_len + 1);
            if (!final_buffer) {
                 client_log(LOG_FATAL, "final realloc for CAT buffer failed");
                 free(content_buffer);
                 if (content_size) *content_size = 0;
                 return NULL;
            }
            final_buffer[final_len] = '\0';
            
            if (content_size) *content_size = final_len;
            client_log(LOG_DEBUG, "CAT for '%s' finished successfully. Total size: %zu bytes.", filename, final_len);
            add_debug_log("<- RECV: [file content, %zu bytes]", final_len);
            return final_buffer;
        }
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
