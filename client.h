#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ncurses.h>
#include <time.h>
#include <pthread.h>
#include <termios.h>
#include <inttypes.h>
#include <fcntl.h>
#include <wchar.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>
#include "common.h"

#define MAX_PATH_SEGMENTS 32

#define ZONE_LIST 0
#define ZONE_HEADER 1
#define ZONE_PATH 2

#define SORT_NAME 0
#define SORT_SIZE 1
#define SORT_TIME 2
#define SORT_TYPE 3

enum OptionalColumn { COL_TIME, COL_SIZE, COL_OWNER, COL_GROUP, COL_PERM, NUM_OPT_COLS };
extern char g_download_dir[1024];

struct DownloadArgs {
    struct FileInfo file_info;
    char curr_path[MAX_PATH];
};

// --- 다운로드 진행 상태 추적 ---
#define MAX_ACTIVE_DOWNLOADS 10
// 다운로드 진행률 표시를 위한 구조체
struct DownStatus {
    char filename[MAX_FILENAME];
    double progress; // 0.0 ~ 1.0
    int active;      // 활성화 여부 
};

extern struct DownStatus g_down_prog[MAX_ACTIVE_DOWNLOADS];
extern pthread_mutex_t g_prog_mutex;

extern struct FileInfo *g_file_list;
extern int g_file_count;
extern int g_selected_item;
extern int g_scroll_offset;
extern char g_server_ip[16];
extern char g_user[MAX_NAME];
extern char g_pass[MAX_NAME];
extern int g_sock_main;
extern char g_status_msg[100];
extern char g_current_path[MAX_PATH];
extern char g_root_path[MAX_PATH];
extern time_t g_scroll_time;
extern long g_scroll_tick;

extern int g_focus_zone;
extern int g_sort_mode;
extern int g_sort_order;

extern const char* G_COL_NAMES_KR[];
extern const int G_COL_WIDTHS[];
extern int g_visible_cols;

extern char g_path_segs[MAX_PATH_SEGMENTS][MAX_NAME];
extern char g_path_routes[MAX_PATH_SEGMENTS][MAX_PATH];
extern int g_path_count;
extern int g_path_index;
extern volatile sig_atomic_t g_cmd_mode;
extern int g_debug_level;

// 완료 큐
#define MAX_COMPLETED_QUEUE 50
extern pthread_mutex_t g_completed_mutex;
extern char g_completed_queue[MAX_COMPLETED_QUEUE][MAX_FILENAME];
extern int g_completed_count;

// client_main.c
void init_tui();
void close_tui();
void sigint_handler(int signo);

// client_ui.c
void draw_tui();
void handle_keys(int ch);
void scroll_text(int y, int x, const char* text, int max_width);
void show_remote_file(const char* filename);
void show_local_file(const char* local_path);

// client_download_path.c
void download_path_mode();

// client_net.c
int auth_client(int sock);
void request_list(int sock);
void cd_client(int sock, const char* dirname);
char* cat_client_fetch(int sock, const char* filename, size_t* content_size);
void* download_thread(void* arg);
void start_downloads();

// client_utils.c
void handle_error(const char *message);
size_t get_mb_len(const char *s);
int read_full(int sock, void* buf, size_t len);
void get_password(char* pass, int max_len);
void format_size(char *buf, size_t buf_size, int64_t size_in_bytes);
int is_binary(const char* filename);
void sort_list();
int compare_files(const void* a, const void* b);
void parse_path();
void init_queue();
void add_queue(const char* filename);
void check_queue();

// client_log.c
enum LogLevel { LOG_INFO, LOG_DEBUG, LOG_WARN, LOG_ERROR, LOG_FATAL };
void client_log(int level, const char *format, ...);
int init_client_log();
void close_client_log();

// client_debug.c
void enter_cmd_mode();
void init_debug_log();
void add_debug_log(const char* format, ...);
void clear_debug_log();
const char* get_key_str(int ch);
ssize_t log_socket_write(int sock, const void *buf, size_t count);
ssize_t log_socket_read(int sock, void *buf, size_t count);
void draw_debug_log(int max_y, int max_x);

// client_upload.c
void upload_mode();
void upload_file_to_server(const char* local,const char* name);


#endif // CLIENT_H

// --- 디버그 패널 관련 ---
#define DEBUG_MSG_COUNT 10
#define DEBUG_MSG_LENGTH 256
extern int g_show_debug;
extern char g_debug_log[DEBUG_MSG_COUNT][DEBUG_MSG_LENGTH];
extern int g_debug_idx;
extern pthread_mutex_t g_debug_mutex;

