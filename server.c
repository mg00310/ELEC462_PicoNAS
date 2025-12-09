/*
 * server.c - Pico NAS 서버
 */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>  
#include <ftw.h>
#include "common.h"

#include <endian.h>
#include <sys/wait.h>
#include <libgen.h>

#ifndef htobe64
#define htobe64(x) __builtin_bswap64(x)
#endif
#ifndef be64toh
#define be64toh(x) __builtin_bswap64(x)
#endif


// 로그용 뮤텍스
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// 클라이언트 스레드 상태를 관리하는 구조체
typedef struct {
    int sock;
    char root_path[MAX_PATH];
    char curr_path[MAX_PATH];
    int is_authed;
} ClientState;

/**
 * @brief 스레드에 안전한 서버 로깅 함수 (타임스탬프 포함).
 * @param format printf와 동일한 형식의 문자열.
 * @param ... 가변 인자.
 */
void server_log(const char *format, ...) {
    pthread_mutex_lock(&log_mutex);

    char time_str[20];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &lt);

    printf("[%s] ", time_str);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    fflush(stdout);

    pthread_mutex_unlock(&log_mutex);
}

// --- 함수 선언 ---
void* handle_client(void* arg);
void do_auth(ClientState* state, char* buffer);
void do_ls(ClientState* state);
void do_cd(ClientState* state, char* buffer);
void do_get(ClientState* state, char* buffer);
void do_cat(ClientState* state, char* buffer);
void do_put(ClientState* state, char* buffer);
void do_getdir(ClientState* state, char* buffer);
void get_perm_str(mode_t mode, char *str);
int write_full(int sock, const void* buf, size_t len);

// --- 서버 메인 ---
int main(int argc, char* argv[]) {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    int port = PORT;

    if (argc > 1) {
        int custom_port = atoi(argv[1]);
        if (custom_port > 0 && custom_port < 65536) port = custom_port;
        else {
            fprintf(stderr, "에러: 유효하지 않은 포트 번호입니다: %s\n", argv[1]);
            exit(1);
        }
    }
    
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        server_log("bind() 에러: %s\n", strerror(errno)); 
        exit(1);
    }
    if (listen(serv_sock, 10) == -1) {
        server_log("listen() 에러: %s\n", strerror(errno)); 
        exit(1);
    }
    
    server_log("Pico NAS 서버 시작... (Port: %d)\n", port);
    
    while (1) {
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        
        server_log("클라이언트 연결됨 (IP: %s)\n", inet_ntoa(clnt_addr.sin_addr));
        
        ClientState* state = malloc(sizeof(ClientState));
        if (!state) { close(clnt_sock); continue; }
        state->sock = clnt_sock;
        state->is_authed = 0;
        memset(state->root_path, 0, MAX_PATH);
        memset(state->curr_path, 0, MAX_PATH);
        pthread_t t_id;
        if (pthread_create(&t_id, NULL, handle_client, (void*)state) != 0) {
            server_log("pthread_create() 에러: %s\n", strerror(errno));
            close(clnt_sock); free(state);
        }
        pthread_detach(t_id);
    }
    close(serv_sock);
    return 0;
}

/**
 * @brief 요청한 길이만큼 데이터를 모두 소켓에 씁니다.
 * @return 성공 시 0, 실패 시 -1.
 */
int write_full(int sock, const void* buf, size_t len) {
    size_t total_written = 0; const char* p = buf;
    while (total_written < len) {
        ssize_t written = write(sock, p + total_written, len - total_written);
        if (written <= 0) return -1;
        total_written += written;
    }
    return 0;
}

/**
 * @brief 클라이언트의 명령어를 처리하는 메인 루프입니다.
 */
void* handle_client(void* arg) {
    ClientState* state = (ClientState*)arg;
    int sock = state->sock;
    char buffer[MAX_PATH * 2];
    ssize_t str_len;

    server_log("클라이언트 (Socket %d) 핸들러 시작.\n", sock);

    while ((str_len = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[str_len] = 0;
        buffer[strcspn(buffer, "\n")] = 0; 
        server_log("Socket %d 수신: \"%s\"\n", sock, buffer);

        if (strncmp(buffer, CMD_AUTH, 4) == 0) do_auth(state, buffer);
        else if (strncmp(buffer, CMD_LS, 2) == 0) {
            if (!state->is_authed) write(sock, RESP_ERR, 4); else do_ls(state);
        }
        else if (strncmp(buffer, CMD_CD, 2) == 0) {
            if (!state->is_authed) write(sock, RESP_ERR, 4); else do_cd(state, buffer);
        }
        else if (strncmp(buffer, CMD_GET, 3) == 0) { 
            if (!state->is_authed) write(sock, RESP_ERR, 4); else do_get(state, buffer);
        }
        else if (strncmp(buffer, CMD_CAT, 3) == 0) { 
            if (!state->is_authed) write(sock, RESP_ERR, 4); else do_cat(state, buffer);
        }
        else if (strncmp(buffer, CMD_PUT, 3) == 0) {
            if (!state->is_authed) write(state->sock, RESP_ERR, 4); else do_put(state, buffer);
        }
        else if (strncmp(buffer, CMD_GETDIR, 4) == 0) {
            if (!state->is_authed) write(state->sock, RESP_ERR, 4); else do_getdir(state, buffer);
        }
        else write(sock, RESP_ERR, 4);
    }
    
    server_log("클라이언트 (Socket %d) 연결 종료.\n", sock);
    close(sock);
    free(state);
    return NULL;
}

/**
 * @brief 'AUTH' 명령어를 처리하여 사용자를 인증합니다.
 */
void do_auth(ClientState* state, char* buffer) {
    char cmd[10], user[MAX_NAME], pass[MAX_NAME];
    if (sscanf(buffer, "%s %s %s", cmd, user, pass) != 3) {
        write(state->sock, RESP_ERR, 4); return;
    }
    FILE* fp = fopen("users.conf", "r");
    if (!fp) {
        server_log("users.conf 열기 실패: %s\n", strerror(errno));
        write(state->sock, RESP_ERR, 4); return;
    }
    char line[MAX_PATH + 100]; 
    int auth_success = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        char file_user[MAX_NAME], file_pass[MAX_NAME], file_root[MAX_PATH];
        if (sscanf(line, "%[^:]:%[^:]:%[^\n]", file_user, file_pass, file_root) == 3) {
            if (strcmp(user, file_user) == 0 && strcmp(pass, file_pass) == 0) {
                state->is_authed = 1;
                realpath(file_root, state->root_path);
                strcpy(state->curr_path, state->root_path);
                auth_success = 1; 
                break;
            }
        }
    }
    fclose(fp);
    if (auth_success) {
        server_log("유저 '%s' 인증 성공. 루트: %s\n", user, state->root_path);
        uint32_t root_len = htonl(strlen(state->root_path));
        uint32_t curr_len = htonl(strlen(state->curr_path));
        write_full(state->sock, RESP_OK, 4);
        write_full(state->sock, &root_len, sizeof(uint32_t));
        write_full(state->sock, state->root_path, strlen(state->root_path));
        write_full(state->sock, &curr_len, sizeof(uint32_t));
        write_full(state->sock, state->curr_path, strlen(state->curr_path));
    } else {
        server_log("유저 '%s' 인증 실패.\n", user);
        write(state->sock, RESP_ERR, 4);
    }
}

/**
 * @brief 'CD' 명령어를 처리하여 현재 작업 디렉터리를 변경합니다.
 */
void do_cd(ClientState* state, char* buffer) {
    char dirname[MAX_PATH];
    if (strlen(buffer) < 5) {
        write(state->sock, RESP_ERR, 4); return;
    }
    
    char* path_ptr = buffer + 4;
    while (*path_ptr && isspace((unsigned char)*path_ptr)) {
        path_ptr++;
    }
    strncpy(dirname, path_ptr, sizeof(dirname) - 1);
    dirname[sizeof(dirname) - 1] = '\0';

    char target_path[MAX_PATH * 2];
    char resolved_path[MAX_PATH];

    server_log("Socket %d CD 요청: \"%s\"\n", state->sock, dirname);

    if (strcmp(dirname, "..") == 0) {
        if (strcmp(state->curr_path, state->root_path) == 0) {
            // 루트 디렉토리에서는 ".." 이동 불가
            write(state->sock, RESP_ERR, 4);
            server_log("Socket %d CD-UP 금지: 이미 루트 디렉토리\n", state->sock);
            return;
        }
        // 현재 경로에서 마지막 '/'를 찾아 부모 디렉토리 경로 생성
        strcpy(target_path, state->curr_path);
        char* last_slash = strrchr(target_path, '/');
        if (last_slash != NULL && last_slash != target_path) {
            *last_slash = '\0'; // 마지막 /를 널 문자로 대체
        } else if (last_slash == target_path && strlen(target_path) > 1) {
            // "/dir" 같은 경우 "/"로 이동
            target_path[1] = '\0';
        }
    } else if (dirname[0] == '/') {
        // 클라이언트가 보낸 절대 경로는 그대로 사용
        strncpy(target_path, dirname, sizeof(target_path) -1);
    } else {
        // 상대 경로는 현재 경로에 덧붙임
        snprintf(target_path, sizeof(target_path), "%s/%s", state->curr_path, dirname);
    }
    target_path[sizeof(target_path) - 1] = '\0';


    if (realpath(target_path, resolved_path) == NULL) {
        server_log("Socket %d realpath 실패: (Invalid Target: %s)\n", state->sock, target_path);
        write(state->sock, RESP_ERR, 4); 
        return;
    }

        // 'Jail' 탈출 방지: 해석된 경로가 루트 경로 내에 있는지 확인

        if (strncmp(resolved_path, state->root_path, strlen(state->root_path)) != 0) {

            server_log("Socket %d Jail 탈출 시도: (Resolved: %s) (Root: %s) -> RESP_ERR 전송\n", state->sock, resolved_path, state->root_path);

            write(state->sock, RESP_ERR, 4); 

            return;

        }

    

        struct stat st;

        if (stat(resolved_path, &st) != 0 || !S_ISDIR(st.st_mode)) {

            server_log("Socket %d 'stat' 실패 또는 디렉터리 아님: (Resolved: %s) -> RESP_ERR 전송\n", state->sock, resolved_path);

            write(state->sock, RESP_ERR, 4); 

            return;

        }

    

        server_log("Socket %d CD 성공 -> RESP_OK 전송 (New Path: %s)\n", state->sock, resolved_path);

        strcpy(state->curr_path, resolved_path);
    
    uint32_t curr_len = htonl(strlen(state->curr_path));
    write_full(state->sock, RESP_OK, 4);
    write_full(state->sock, &curr_len, sizeof(uint32_t));
    write_full(state->sock, state->curr_path, strlen(state->curr_path));
}

#include <ftw.h>

static uint64_t dir_size_accum;

int nftw_cb(const char* fpath, const struct stat* sb,
            int typeflag, struct FTW* ftwbuf)
{
    if (typeflag == FTW_F)
        dir_size_accum += sb->st_size;
    return 0;
}

uint64_t calc_dir_size(const char* path) {
    dir_size_accum = 0;
    nftw(path, nftw_cb, 20, FTW_PHYS);
    return dir_size_accum;
}


/**
 * @brief 'LS' 명령어를 처리하여 현재 디렉터리의 파일 목록을 전송합니다.
 */
void do_ls(ClientState* state) {
    DIR *dir; struct dirent *entry; struct stat st;
    char full_path[MAX_PATH * 2];

    dir = opendir(state->curr_path);
    if (!dir) { write(state->sock, RESP_ERR, 4); return; }

    // 파일 카운트 계산
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        count++;
    }
    rewinddir(dir); 

    // 1) 시작 패킷
    write_full(state->sock, RESP_LS_S, 4);
    uint32_t c = htonl(count);
    write_full(state->sock, &c, 4);

    // 2) 파일 목록 개별 전송
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", state->curr_path, entry->d_name);
        stat(full_path, &st);

        char type = S_ISDIR(st.st_mode)?'d':'f';
        uint64_t size = htobe64((uint64_t)st.st_size);

        // 파일명 길이 (가변 전송 가능)
        uint32_t nameLen = htonl(strlen(entry->d_name));

        write_full(state->sock, &type, 1);
        write_full(state->sock, &size, 8);
        write_full(state->sock, &nameLen, 4);
        write_full(state->sock, entry->d_name, strlen(entry->d_name));
    }

    write_full(state->sock, RESP_LS_E, 4);
    closedir(dir);
}


/**
 * @brief 'GET' 명령어를 처리하여 파일을 클라이언트에 전송합니다.
 */
void do_get(ClientState* state, char* buffer) {
    if (strlen(buffer) < 5) { write(state->sock, RESP_ERR, 4); return; }

    char* path_ptr = buffer + 4; // CMD_GET is 4 chars
    while (*path_ptr && isspace((unsigned char)*path_ptr)) {
        path_ptr++;
    }
    
    char filename_from_client[MAX_PATH]; // Use a temporary variable to hold the parsed path
    strncpy(filename_from_client, path_ptr, sizeof(filename_from_client) - 1);
    filename_from_client[sizeof(filename_from_client) - 1] = '\0';

    char target_path[MAX_PATH * 2];
    char resolved_path[MAX_PATH];

    server_log("Socket %d GET 요청: %s\n", state->sock, filename_from_client); // Log the client's original request

    // 경로 유형에 따라 target_path를 다르게 구성
    if (filename_from_client[0] == '/') {
        // 클라이언트가 보낸 절대 경로는 그대로 사용
        strncpy(target_path, filename_from_client, sizeof(target_path) - 1);
    } else {
        // 상대 경로는 현재 경로에 덧붙임
        snprintf(target_path, sizeof(target_path), "%s/%s", state->curr_path, filename_from_client);
    }
    target_path[sizeof(target_path) - 1] = '\0'; // Ensure null-termination
    
    // 1. realpath()로 실제 경로 해석
    if (realpath(target_path, resolved_path) == NULL) {
        server_log("Socket %d GET realpath 실패: %s -> %s\n", state->sock, target_path, strerror(errno));
        write(state->sock, RESP_ERR, 4); return;
    }

    // 2. 'Jail' 탈출 방지
    if (strncmp(resolved_path, state->root_path, strlen(state->root_path)) != 0) {
        server_log("Socket %d GET Jail 탈출 시도: %s\n", state->sock, resolved_path);
        write(state->sock, RESP_ERR, 4); return;
    }
    
    // 3. 파일 상태 확인 (일반 파일인지)
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        server_log("Socket %d GET stat 실패 또는 일반 파일 아님: %s\n", state->sock, resolved_path);
        write(state->sock, RESP_ERR, 4); return;
    }

    // 4. 파일 열기
    int fd = open(resolved_path, O_RDONLY);
    if (fd == -1) {
        server_log("Socket %d GET open 실패: %s -> %s\n", state->sock, resolved_path, strerror(errno));
        write(state->sock, RESP_ERR, 4); return;
    }

    // 5. 파일 전송 시작 응답 (헤더 + 파일 크기)
    int64_t file_size_net = htobe64(st.st_size);
    write_full(state->sock, RESP_GET_S, 4);
    write_full(state->sock, &file_size_net, sizeof(int64_t));

    // 6. 파일 내용 전송
    char file_buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, file_buffer, sizeof(file_buffer))) > 0) {
        if (write_full(state->sock, file_buffer, bytes_read) != 0) {
            break; // 클라이언트 연결 끊김
        }
    }
    close(fd);
    
    server_log("Socket %d 파일 전송 완료: %s\n", state->sock, resolved_path);
}

void do_put(ClientState* state, char* buffer) {
    char filename[MAX_FILENAME];
    char fullpath[MAX_PATH];

    // 1. 파일명 파싱
    sscanf(buffer + 4, "%s", filename);
    snprintf(fullpath, sizeof(fullpath), "%s/%s", state->curr_path, filename);

    // 2. Jail 확인 (curr_path 기반만 허용)
    char resolved_curr[MAX_PATH];
    if (!realpath(state->curr_path, resolved_curr)) {
        write(state->sock, RESP_ERR, 4);
        return;
    }
    if (strncmp(resolved_curr, state->root_path, strlen(state->root_path)) != 0) {
        write(state->sock, RESP_ERR, 4);
        return;
    }

    // 3. 파일 생성 (fail 시 확실한 반환)
    int fd = open(fullpath, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { 
        write(state->sock, RESP_ERR, 4); 
        return; 
    }

    // 4. 업로드 시작 승인
    if(write(state->sock, RESP_PUT_S, 4) <= 0){
        close(fd); 
        return;
    }

    // 5. 파일 크기 수신 (fail 방지)
    int64_t file_size_net = 0;
    if (read_full(state->sock, &file_size_net, sizeof(file_size_net)) < 0) {
        close(fd);
        return;
    }
    int64_t filesize = be64toh(file_size_net);

    // 6. 파일 내용 수신 (부분패킷 포함 → 안정화)
    char buf[4096];
    int64_t received = 0;

    while(received < filesize){
        ssize_t r = read(state->sock, buf, sizeof(buf));
        if (r <= 0) {                          // 🔥 EOF/중단 감지
            close(fd);
            write(state->sock, RESP_ERR, 4);   // 클라이언트 block 방지
            return;
        }
        if(write(fd, buf, r) != r){            // write 실패 예외 처리
            close(fd);
            write(state->sock, RESP_ERR, 4);
            return;
        }
        received += r;
    }
    close(fd);

    write(state->sock, RESP_PUT_E, 4);
    server_log("✔ 업로드 완료: %s (%ld bytes)\n", filename, filesize);
}



void do_getdir(ClientState* state, char* buffer) {
    // 1. GETDIR 파라미터 파싱
    char full_path[MAX_PATH];
    char* p = buffer + 4;
    while (*p == ' ') p++;
    strncpy(full_path, p, sizeof(full_path) - 1);
    full_path[sizeof(full_path)-1] = '\0';

    char resolved[MAX_PATH];
    if (realpath(full_path, resolved) == NULL) {
        write(state->sock, RESP_ERR, 4);
        return;
    }

    // 2. Jail 체크
    if (strncmp(resolved, state->root_path, strlen(state->root_path)) != 0) {
        write(state->sock, RESP_ERR, 4);
        return;
    }

    // 3. 디렉터리인지 확인
    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        write(state->sock, RESP_ERR, 4);
        return;
    }

    // 4. tar 파일 생성
    char tmp_tar[256];
    snprintf(tmp_tar, sizeof(tmp_tar), "/tmp/piconas_%d.tar", getpid());

    pid_t pid = fork();
    if (pid == 0) {
        execlp("tar", "tar", "-cf", tmp_tar, "-C",
               dirname(resolved), basename(resolved), NULL);
        exit(1);
    }
    int status;
    waitpid(pid, &status, 0);

    // 5. tar 크기 전송
    struct stat ts;
    stat(tmp_tar, &ts);
    uint64_t size = ts.st_size;
    uint64_t size_net = htobe64(size);

    write_full(state->sock, RESP_GETDIR_S, 4);
    write_full(state->sock, &size_net, sizeof(size_net));

    // 6. tar 파일 내용 전송
    int fd = open(tmp_tar, O_RDONLY);
    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        write_full(state->sock, buf, r);
    }
    close(fd);

    // 7. 임시 tar 삭제
    unlink(tmp_tar);

    server_log("폴더 전송 완료: %s\n", resolved);
}


/**
 * @brief 'CAT' 명령어를 처리하여 파일 내용을 클라이언트에 전송합니다.
 */
void do_cat(ClientState* state, char* buffer) {
    if (strlen(buffer) < 5) { write(state->sock, RESP_ERR, 4); return; }

    char* path_ptr = buffer + 4;
    while (*path_ptr && isspace((unsigned char)*path_ptr)) {
        path_ptr++;
    }
    
    char target_path[MAX_PATH * 2];
    snprintf(target_path, sizeof(target_path), "%s/%s", state->curr_path, path_ptr);

    char resolved_path[MAX_PATH];
    server_log("Socket %d CAT 요청: %s\n", state->sock, target_path);

    if (realpath(target_path, resolved_path) == NULL) {
        server_log("Socket %d CAT realpath 실패: %s -> %s\n", state->sock, target_path, strerror(errno));
        write(state->sock, RESP_ERR, 4); return;
    }

    // 2. 'Jail' 탈출 방지
    if (strncmp(resolved_path, state->root_path, strlen(state->root_path)) != 0) {
        server_log("Socket %d CAT Jail 탈출 시도: %s -> RESP_ERR 전송\n", state->sock, resolved_path);
        write(state->sock, RESP_ERR, 4); return;
    }
    
    // 3. 파일 상태 확인 (일반 파일인지)
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        server_log("Socket %d CAT stat 실패 또는 일반 파일 아님: %s -> RESP_ERR 전송\n", state->sock, resolved_path);
        write(state->sock, RESP_ERR, 4); return;
    }

    // 4. 파일 열기
    int fd = open(resolved_path, O_RDONLY);
    if (fd == -1) {
        server_log("Socket %d CAT open 실패: %s -> %s -> RESP_ERR 전송\n", state->sock, resolved_path, strerror(errno));
        write(state->sock, RESP_ERR, 4); return;
    }

    // 5. 파일 전송 시작 응답 (헤더 + 파일 크기)
    server_log("Socket %d CAT 성공 -> RESP_OK 전송, 파일 내용 전송 시작: %s (크기: %ld)\n", state->sock, resolved_path, st.st_size);
    write_full(state->sock, RESP_OK, 4);

    char file_buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, file_buffer, sizeof(file_buffer))) > 0) {
        if (write_full(state->sock, file_buffer, bytes_read) != 0) {
            break; 
        }
    }
    close(fd);
    
    write_full(state->sock, RESP_CAT_E, 4);
    server_log("Socket %d 파일 내용 전송 완료: %s\n", state->sock, resolved_path);
}


/**
 * @brief 파일 권한(mode_t)을 "rwxr-xr--" 형태의 문자열로 변환합니다.
 */
void get_perm_str(mode_t mode, char *str) {
    strcpy(str, "----------");
    if (S_ISDIR(mode)) str[0] = 'd';
    if (S_ISLNK(mode)) str[0] = 'l';
    if (mode & S_IRUSR) str[1] = 'r';
    if (mode & S_IWUSR) str[2] = 'w';
    if (mode & S_IXUSR) str[3] = 'x';
    if (mode & S_IRGRP) str[4] = 'r';
    if (mode & S_IWGRP) str[5] = 'w';
    if (mode & S_IXGRP) str[6] = 'x';
    if (mode & S_IROTH) str[7] = 'r';
    if (mode & S_IWOTH) str[8] = 'w';
    if (mode & S_IXOTH) str[9] = 'x';
}
