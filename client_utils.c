#include "client.h"
#include <strings.h> // For strcasecmp

// 미리보기 불가능한 파일 확장자 목록
static const char* unviewable_extensions[] = {
    ".mp3", ".mp4", ".avi", ".mkv", ".mov", ".flv", ".wmv",
    ".pdf", ".zip", ".tar", ".gz", ".7z", ".rar",
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".ico",
    ".exe", ".bin", ".dll", ".so", ".o", ".a",
    ".sqlite", ".db", ".dat", ".class", ".jar",
    NULL // 리스트의 끝을 표시
};

/**
 * @brief 파일 확장자를 기반으로 터미널에서 미리보기 불가능한 파일인지 확인합니다.
 * @param filename 확인할 파일 이름.
 * @return 미리보기 불가능하면 1, 가능하면 0.
 */
int is_binary(const char* filename) {
    const char* dot = strrchr(filename, '.');
    // 확장자가 없거나, 파일명 시작이 .이거나, 확장자가 1글자 이하인 경우 (예: .bashrc, .profile은 뷰 가능으로 간주)
    if (!dot || dot == filename || strlen(dot) < 2) return 0;

    for (int i = 0; unviewable_extensions[i] != NULL; i++) {
        if (strcasecmp(dot, unviewable_extensions[i]) == 0) {
            return 1; // 미리보기 불가능한 확장자 발견
        }
    }
    return 0; // 미리보기 가능한 확장자
}


// --- 유틸리티 함수 ---
void handle_error(const char *message) {
    close_tui(); 
    perror(message); 
    exit(1);
}

size_t get_mb_len(const char *s) {
    if (!s) return 0;
    return mbstowcs(NULL, s, 0);
}

int read_full(int sock, void* buf, size_t len) {
    size_t total_read = 0; 
    char* p = buf;
    while (total_read < len) {
        ssize_t bytes_read = read(sock, p + total_read, len - total_read);
        if (bytes_read <= 0) return -1;
        total_read += bytes_read;
    }
    return 0;
}

void get_password(char* pass, int max_len) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fgets(pass, max_len, stdin);
    pass[strcspn(pass, "\n")] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

void format_size(char *buf, size_t buf_size, int64_t size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double d_size = size;
    while (d_size >= 1024 && i < 4) {
        d_size /= 1024.0;
        i++;
    }
    if (i == 0) {
        snprintf(buf, buf_size, "%" PRId64 " %s", (int64_t)d_size, units[i]);
    } else {
        snprintf(buf, buf_size, "%.1f %s", d_size, units[i]);
    }
}

// --- 정렬 로직 ---
int compare_files(const void* a, const void* b) {
    const struct FileInfo *fa = (const struct FileInfo*)a;
    const struct FileInfo *fb = (const struct FileInfo*)b;
    if (strcmp(fa->filename, "..") == 0) return -1;
    if (strcmp(fb->filename, "..") == 0) return 1;
    int result = 0;
    switch (g_sort_mode) {
        case SORT_SIZE:
            if (fa->size > fb->size) result = 1;
            else if (fa->size < fb->size) result = -1;
            break;
        case SORT_TIME:
            if (fa->mod_time_raw > fb->mod_time_raw) result = 1;
            else if (fa->mod_time_raw < fb->mod_time_raw) result = -1;
            break;
        case SORT_TYPE:
            result = (int)fa->type - (int)fb->type;
            break;
        case SORT_NAME: default:
            result = strcmp(fa->filename, fb->filename);
            break;
    }
    return result * g_sort_order;
}

void sort_list() {
    if (g_file_count > 0 && g_file_list) {
        qsort(g_file_list, g_file_count, sizeof(struct FileInfo), compare_files);
    }
}

/**
 * @brief 현재 경로 문자열을 분석하여 경로 세그먼트 배열을 생성합니다.
 *
 * g_current_path에서 시작하여 g_root_path에 도달할 때까지
 * 경로를 한 단계(token)씩 잘라내며 segments와 paths 배열을 생성합니다.
 * 이로 인해 Jail(root) 경로 계산이 완벽하게 일치합니다.
 */
void parse_path() {
    g_path_count = 0;
    g_path_index = 0;
    
    char temp_path[MAX_PATH];
    char path_builder[MAX_PATH];
    strcpy(path_builder, g_current_path);
    
    size_t root_len = strlen(g_root_path);
    int i = 0;

    while (i < MAX_PATH_SEGMENTS) {
        // 1. 현재 path_builder 경로를 paths 배열에 저장
        strcpy(g_path_routes[i], path_builder);

        // 2. path_builder에서 마지막 세그먼트(폴더명)를 분리
        char* last_slash = strrchr(path_builder, '/');
        
        if (last_slash == NULL || last_slash == path_builder) {
            if (strlen(path_builder) > 1 && path_builder[0] == '/') {
                strncpy(g_path_segs[i], last_slash + 1, MAX_NAME - 1);
            } else {
                strncpy(g_path_segs[i], path_builder, MAX_NAME - 1);
            }
            g_path_segs[i][MAX_NAME - 1] = 0;
            i++;
            break; // 루트에 도달했으므로 종료
        } else {
            strncpy(g_path_segs[i], last_slash + 1, MAX_NAME - 1);
            g_path_segs[i][MAX_NAME - 1] = 0;
            i++;
        }

        // 3. path_builder를 부모 경로로 수정
        *last_slash = '\0';
        
        // 4. [Jail 검사] 부모 경로가 g_root_path를 벗어났는지 확인
        if (strlen(path_builder) < root_len ||
            strncmp(path_builder, g_root_path, root_len) != 0)
        {
            last_slash = strrchr(g_root_path, '/');
            if (last_slash != NULL && last_slash != g_root_path) {
                strncpy(g_path_segs[i], last_slash + 1, MAX_NAME - 1);
            } else if (g_root_path[0] == '/') {
                 strncpy(g_path_segs[i], g_root_path + 1, MAX_NAME - 1);
            } else {
                 strncpy(g_path_segs[i], g_root_path, MAX_NAME - 1);
            }
            g_path_segs[i][MAX_NAME - 1] = 0;
            strcpy(g_path_routes[i], g_root_path);
            i++;
            break;
        }
        
        // 5. path_builder가 g_root_path와 같아졌는지 확인
        if (strcmp(path_builder, g_root_path) == 0) {
             last_slash = strrchr(path_builder, '/');
             if (last_slash != NULL && last_slash != path_builder) {
                 strncpy(g_path_segs[i], last_slash + 1, MAX_NAME - 1);
             } else if (path_builder[0] == '/') {
                 if (strlen(path_builder) == 1) // "/"
                    strncpy(g_path_segs[i], path_builder, MAX_NAME - 1);
                 else // "/home"
                    strncpy(g_path_segs[i], path_builder + 1, MAX_NAME - 1);
             }
             g_path_segs[i][MAX_NAME - 1] = 0;
             strcpy(g_path_routes[i], path_builder);
             i++;
             break;
        }
    }
    
    g_path_count = i;
    
    // 6. 배열 뒤집기 (현재는 역순으로 저장되어 있음)
    for (int j = 0; j < g_path_count / 2; j++) {
        strcpy(temp_path, g_path_segs[j]);
        strcpy(g_path_segs[j], g_path_segs[g_path_count - 1 - j]);
        strcpy(g_path_segs[g_path_count - 1 - j], temp_path);
        
        strcpy(temp_path, g_path_routes[j]);
        strcpy(g_path_routes[j], g_path_routes[g_path_count - 1 - j]);
        strcpy(g_path_routes[g_path_count - 1 - j], temp_path);
    }
    
    g_path_index = g_path_count - 1;
    if (g_path_index < 0) g_path_index = 0;
}

// --- 다운로드 완료 큐 로직 ---
pthread_mutex_t g_completed_mutex;
char g_completed_queue[MAX_COMPLETED_QUEUE][MAX_FILENAME];
int g_completed_count = 0;

void init_queue() {
    pthread_mutex_init(&g_completed_mutex, NULL);
    g_completed_count = 0;
}

void add_queue(const char* filename) {
    pthread_mutex_lock(&g_completed_mutex);
    if (g_completed_count < MAX_COMPLETED_QUEUE) {
        strncpy(g_completed_queue[g_completed_count], filename, MAX_FILENAME - 1);
        g_completed_queue[g_completed_count][MAX_FILENAME - 1] = '\0';
        g_completed_count++;
    }
    pthread_mutex_unlock(&g_completed_mutex);
}

void check_queue() {
    pthread_mutex_lock(&g_completed_mutex);
    if (g_completed_count > 0) {
        for (int i = 0; i < g_completed_count; i++) {
            for (int j = 0; j < g_file_count; j++) {
                if (strcmp(g_completed_queue[i], g_file_list[j].filename) == 0) {
                    g_file_list[j].is_downloaded = 1;
                    break;
                }
            }
        }
        g_completed_count = 0; // 큐 비우기
    }
    pthread_mutex_unlock(&g_completed_mutex);
}

    
    