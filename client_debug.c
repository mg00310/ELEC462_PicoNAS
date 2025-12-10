#include "client.h"
#include <dirent.h>
#include <stdarg.h>

void show_log_list();
const char* get_proto_str(const char* code, size_t len);

 
/**
 * @brief 디버그 메시지 버퍼와 뮤텍스를 초기화합니다.
 */
void init_debug_log() {
    pthread_mutex_init(&g_debug_mutex, NULL);
    clear_debug_log();
}

/**
 * @brief 디버그 메시지 버퍼를 초기화합니다.
 */
void clear_debug_log() {
    pthread_mutex_lock(&g_debug_mutex);
    for (int i = 0; i < DEBUG_MSG_COUNT; i++) {
        g_debug_log[i][0] = '\0';
    }
    g_debug_idx = 0;
    pthread_mutex_unlock(&g_debug_mutex);
}

/**
 * @brief UI 디버그 패널에 표시될 메시지를 추가합니다. (순환 버퍼)
 */
void add_debug_log(const char* format, ...) {
    pthread_mutex_lock(&g_debug_mutex);

    char buffer[DEBUG_MSG_LENGTH];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    strncpy(g_debug_log[g_debug_idx], buffer, DEBUG_MSG_LENGTH - 1);
    g_debug_log[g_debug_idx][DEBUG_MSG_LENGTH - 1] = '\0';
    
    g_debug_idx = (g_debug_idx + 1) % DEBUG_MSG_COUNT;

    pthread_mutex_unlock(&g_debug_mutex);
}

void enter_cmd_mode() {
    snprintf(g_status_msg, sizeof(g_status_msg), "명령어 입력: (d)디버그 패널 (l)로그 보기 (q)취소");
    draw_tui(); 

    nodelay(stdscr, FALSE); 
    int ch = getch();
    nodelay(stdscr, TRUE); 
    
    switch(ch) {
        case 'd':
            g_show_debug = !g_show_debug;
            if (g_show_debug) {
                snprintf(g_status_msg, sizeof(g_status_msg), "디버그 패널 ON");
                client_log(LOG_INFO, "Debug panel enabled.");
            } else {
                snprintf(g_status_msg, sizeof(g_status_msg), "디버그 패널 OFF");
                client_log(LOG_INFO, "Debug panel disabled.");
            }
            break;
        case 'l':
            client_log(LOG_INFO, "Entering log selection screen.");
            show_log_list();
            break;
        case 'q':
        case 27:
            snprintf(g_status_msg, sizeof(g_status_msg), " "); 
            break;
        default:
            snprintf(g_status_msg, sizeof(g_status_msg), "알 수 없는 명령: %c", ch);
            break;
    }
    g_cmd_mode = 0; 
}

const char* get_proto_str(const char* code, size_t len) {
    if (len < 4) return "데이터/알수없음";
    if (strncmp(code, CMD_AUTH, 4) == 0) return "AUTH (인증)";
    if (strncmp(code, CMD_LS, 4) == 0) return "LS (목록)";
    if (strncmp(code, CMD_CD, 4) == 0) return "CD (이동)";
    if (strncmp(code, CMD_GET, 4) == 0) return "GET (다운로드)";
    if (strncmp(code, CMD_CAT, 4) == 0) return "CAT (미리보기)";
    if (strncmp(code, RESP_OK, 4) == 0) return "OK (성공)";
    if (strncmp(code, RESP_ERR, 4) == 0) return "ERR (오류)";
    if (strncmp(code, RESP_LS_S, 4) == 0) return "LS_S (목록 시작)";
    if (strncmp(code, RESP_LS_E, 4) == 0) return "LS_E (목록 종료)";
    if (strncmp(code, RESP_GET_S, 4) == 0) return "GET_S (받기 시작)";
    if (strncmp(code, RESP_CAT_E, 4) == 0) return "CAT_E (보기 종료)";
    
    // For data chunks, just describe them
    static char data_desc[50];
    snprintf(data_desc, sizeof(data_desc), "데이터 (%zu 바이트)", len);
    return data_desc;
}

const char* get_key_str(int ch) {
    if (ch >= 32 && ch <= 126) {
        static char ascii_key[2] = {0};
        ascii_key[0] = (char)ch;
        return ascii_key;
    }
    switch (ch) {
        case KEY_UP: return "↑ (UP)";
        case KEY_DOWN: return "↓ (DOWN)";
        case KEY_LEFT: return "← (LEFT)";
        case KEY_RIGHT: return "→ (RIGHT)";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_PPAGE: return "PgUp";
        case KEY_NPAGE: return "PgDn";
        case '\n': return "Enter";
        case ' ': return "Space";
        case 27: return "ESC";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_DC: return "Delete";
        default:
            {
                static char unknown_key[20];
                snprintf(unknown_key, sizeof(unknown_key), "알 수 없는 키 (%d)", ch);
                return unknown_key;
            }
    }
}

ssize_t log_socket_write(int sock, const void *buf, size_t count) {
    add_debug_log("-> 송신: %s", get_proto_str(buf, count));
    return write(sock, buf, count);
}

ssize_t log_socket_read(int sock, void *buf, size_t count) {
    int result = read_full(sock, buf, count);
    if (result == 0) {
        add_debug_log("<- 수신: %s", get_proto_str(buf, count));
    }
    return result;
}

void draw_debug_log(int max_y, int max_x) {
    int debug_panel_height = g_show_debug ? (DEBUG_MSG_COUNT + 2) : 0;

    if (g_show_debug) {
        int panel_start_y = max_y - debug_panel_height;
        // Separator line
        attron(COLOR_PAIR(11));
        mvprintw(panel_start_y, 0, "%*s", max_x, " ");
        mvprintw(panel_start_y, 0, "--- 디버그 패널 ---");
        attroff(COLOR_PAIR(11));

        pthread_mutex_lock(&g_debug_mutex);
        for (int i = 0; i < DEBUG_MSG_COUNT; i++) {
            int msg_idx = (g_debug_idx + i) % DEBUG_MSG_COUNT;
            mvprintw(panel_start_y + 1 + i, 0, "%.*s", max_x, g_debug_log[msg_idx]);
        }
        pthread_mutex_unlock(&g_debug_mutex);
    }
}

void show_log_list() {
    DIR *d = opendir("logs");
    if (!d) {
        snprintf(g_status_msg, sizeof(g_status_msg), "'logs' 디렉터리를 열 수 없습니다.");
        return;
    }

    char** log_files = NULL;
    int file_count = 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG && strstr(dir->d_name, ".log")) {
            log_files = (char**)realloc(log_files, (file_count + 1) * sizeof(char*));
            log_files[file_count++] = strdup(dir->d_name);
        }
    }
    closedir(d);

    if (file_count == 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "로그 파일이 없습니다.");
        if(log_files) free(log_files);
        return;
    }

    int selected = 0;
    int ch;
    nodelay(stdscr, FALSE);
    while(1) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        erase();

        attron(COLOR_PAIR(11));
        mvprintw(0, 0, "%*s", max_x, " ");
        mvprintw(0, 0, "로그 파일 선택");
        attroff(COLOR_PAIR(11));

        for (int i = 0; i < file_count; i++) {
            if (i >= max_y - 2) break;
            if (i == selected) {
                attron(COLOR_PAIR(7)); 
                mvprintw(i + 1, 0, "> %s", log_files[i]);
                attroff(COLOR_PAIR(7));
            } else {
                mvprintw(i + 1, 0, "  %s", log_files[i]);
            }
        }

        attron(COLOR_PAIR(11));
        mvprintw(max_y - 1, 0, "%*s", max_x, " ");
        mvprintw(max_y - 1, 0, "[↑↓]이동 [Enter]선택 [Q]나가기");
        attroff(COLOR_PAIR(11));
        
        refresh();
        ch = getch();

        if (ch == KEY_UP && selected > 0) selected--;
        else if (ch == KEY_DOWN && selected < file_count - 1) selected++;
        else if (ch == '\n' || ch == KEY_ENTER) {
            char full_path[200];
            snprintf(full_path, sizeof(full_path), "logs/%s", log_files[selected]);
            show_local_file(full_path);
            break;
        } else if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        }
    }
    nodelay(stdscr, TRUE);

    for (int i = 0; i < file_count; i++) free(log_files[i]);
    if (log_files) free(log_files);
}
