// ================= client_main.c ==================
#include "client.h"
#define _XOPEN_SOURCE 700
#include <unistd.h>

// --- 전역 변수 정의 ---
struct FileInfo *g_file_list = NULL;
int g_file_count = 0;
int g_selected_item = 0;
int g_scroll_offset = 0;
char g_server_ip[16];
char g_user[MAX_NAME];
char g_pass[MAX_NAME];
int g_sock_main = 0;
char g_status_msg[100] = {0};
char g_current_path[MAX_PATH] = "/";
char g_root_path[MAX_PATH] = "/";
time_t g_scroll_time = 0;
long g_scroll_tick = 0;
char g_download_dir[MAX_PATH] = "./";

int g_focus_zone = ZONE_LIST;
int g_sort_mode = SORT_NAME;
int g_sort_order = 1;

const char* G_COL_NAMES_KR[] = {
    " 수정시간          ", " 크기     ", " 소유자   ", " 그룹     ", " 권한         "
};
const int G_COL_WIDTHS[] = {18, 10, 10, 10, 13};
int g_visible_cols = 0;

char g_path_segs[MAX_PATH_SEGMENTS][MAX_NAME];
char g_path_routes[MAX_PATH_SEGMENTS][MAX_PATH];
int g_path_count = 0;
int g_path_index = 0;

// 다운로드 진행 상태
struct DownStatus g_down_prog[MAX_ACTIVE_DOWNLOADS];
pthread_mutex_t g_prog_mutex;


// --- TUI 초기화 & 종료 ---
void init_tui() {
    setlocale(LC_ALL, "");
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); curs_set(0);
    nodelay(stdscr, TRUE);

    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_YELLOW);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
    init_pair(4, COLOR_BLUE, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_RED);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(7, COLOR_BLACK, COLOR_CYAN);
    init_pair(8, COLOR_WHITE, COLOR_BLACK);
    init_pair(9, COLOR_WHITE, COLOR_BLACK);
    init_pair(10, COLOR_BLACK, COLOR_WHITE);
    init_pair(11, COLOR_BLACK, COLOR_GREEN);
    init_pair(12, COLOR_BLUE, COLOR_YELLOW);
}

void close_tui() {
    if (g_file_list) free(g_file_list);
    endwin();
}


// ===================== Main ======================
int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "사용법: %s <Server IP>\n", argv[0]);
        exit(1);
    }

    strncpy(g_server_ip, argv[1], 15);
    g_server_ip[15] = 0;

    struct sockaddr_in serv_addr;
    g_sock_main = socket(PF_INET, SOCK_STREAM, 0);
    if (g_sock_main == -1) handle_error("socket() 에러");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(g_server_ip);
    serv_addr.sin_port = htons(PORT);

    if (connect(g_sock_main, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        handle_error("connect() 에러");

    printf("서버(%s)에 연결됨. 인증을 시작합니다.\n", argv[1]);

    if (!auth_client(g_sock_main)) {
        printf("인증 실패.\n");
        close(g_sock_main);
        return 1;
    }

    printf("인증 성공! TUI 실행 준비...\n");
    sleep(1);

    init_tui();
    init_queue();
    draw_tui();
    pthread_mutex_init(&g_prog_mutex, NULL);

    for(int i=0;i<MAX_ACTIVE_DOWNLOADS;i++){
        g_down_prog[i].active = 0;
        g_down_prog[i].progress = 0.0;
    }

    request_list(g_sock_main);

    draw_tui();
    refresh();

    g_scroll_time = time(NULL);

    // 🔥 최초 실행시 화면이 안뜨던 문제 해결 핵심
    draw_tui();   //  ← UI 즉시 렌더링 추가

    while (1) {
        int ch = getch();
        //debuf
        mvprintw(0,0, "RUNNING LOOP"); refresh();
        int needs_redraw = 0;

        if (ch != ERR) {
            handle_keys(ch);
            needs_redraw = 1;
        }

        check_queue();

        time_t now = time(NULL);
        if (now > g_scroll_time) {
            g_scroll_time = now;
            g_scroll_tick++;
            needs_redraw = 1;
        }

        int active = 0;
        pthread_mutex_lock(&g_prog_mutex);
        for(int i=0;i<MAX_ACTIVE_DOWNLOADS;i++){
            if(g_down_prog[i].active){
                active = 1; break;
            }
        }
        pthread_mutex_unlock(&g_prog_mutex);

        if(active) needs_redraw = 1;
        if(needs_redraw) draw_tui();

        usleep(50000);
    }

    close_tui();
    close(g_sock_main);
    return 0;
}
