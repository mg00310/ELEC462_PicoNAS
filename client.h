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

// 다운로드 저장 위치
extern char g_download_dir[MAX_PATH];

struct DownloadArgs {
    struct FileInfo file_info;
    char curr_path[MAX_PATH];
};

#define MAX_ACTIVE_DOWNLOADS 10
struct DownStatus {
    char filename[MAX_FILENAME];
    double progress;
    int active;
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

// 완료 큐
#define MAX_COMPLETED_QUEUE 50
extern pthread_mutex_t g_completed_mutex;
extern char g_completed_queue[MAX_COMPLETED_QUEUE][MAX_FILENAME];
extern int g_completed_count;

// main
void init_tui();
void close_tui();

// ui
void draw_tui();
void handle_keys(int ch);
void scroll_text(int y, int x, const char* text, int max_width);

void download_path_mode();   // ★ 추가

// net
int auth_client(int sock);
void request_list(int sock);
void cd_client(int sock, const char* dirname);
void* download_thread(void* arg);
void start_downloads();

// utils
void handle_error(const char *message);
size_t get_mb_len(const char *s);
int read_full(int sock, void* buf, size_t len);
void get_password(char* pass, int max_len);
void format_size(char *buf, size_t buf_size, int64_t size);
void sort_list();
int compare_files(const void* a, const void* b);
void parse_path();
void init_queue();
void add_queue(const char* filename);
void check_queue();

// upload ui
void upload_mode();
void upload_file_to_server(const char* local,const char* name);

#endif
