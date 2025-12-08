#include "client.h"

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

int g_focus_zone = ZONE_LIST;
int g_sort_mode = SORT_NAME;
int g_sort_order = 1;

const char* G_COL_NAMES_KR[] = { " 수정시간          ", " 크기     ", " 소유자   ", " 그룹     ", " 권한         " };
const int G_COL_WIDTHS[] = { 18, 10, 10, 10, 13 }; // [UI Fix] '권한' 컬럼 너비 조정 (14->13)
int g_visible_cols = 0;

char g_path_segs[MAX_PATH_SEGMENTS][MAX_NAME];
char g_path_routes[MAX_PATH_SEGMENTS][MAX_PATH];
int g_path_count = 0;
int g_path_index = 0;

// 다운로드 진행 상태 공유 변수
struct DownStatus g_down_prog[MAX_ACTIVE_DOWNLOADS];
pthread_mutex_t g_prog_mutex;
volatile sig_atomic_t g_cmd_mode = 0;
int g_debug_level = 0;

// 디버그 패널 관련
int g_show_debug = 0;
char g_debug_log[DEBUG_MSG_COUNT][DEBUG_MSG_LENGTH];
int g_debug_idx = 0;
pthread_mutex_t g_debug_mutex;


// --- TUI 초기화 및 종료 ---
void init_tui() {
    setlocale(LC_ALL, "");
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); curs_set(0);
    nodelay(stdscr, TRUE);
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_YELLOW); // 하이라이트
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
    init_pair(4, COLOR_BLUE, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_RED);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(7, COLOR_BLACK, COLOR_CYAN);
    init_pair(8, COLOR_WHITE, COLOR_BLACK); // A_DIM 대용 (회색)
    init_pair(9, COLOR_WHITE, COLOR_BLACK); // A_DIM 대용 (회색)
    init_pair(10, COLOR_BLACK, COLOR_WHITE);
    init_pair(11, COLOR_BLACK, COLOR_GREEN); // 기본 경로 바
    init_pair(12, COLOR_BLUE, COLOR_YELLOW); // 다운로드 완료 항목용 (파란 글씨/노란 배경)
}

void close_tui() {
    if (g_file_list) free(g_file_list);
    close_client_log();
    endwin();
}

/**
 * @brief SIGINT (Ctrl+C) 신호 핸들러.
 */
void sigint_handler(int signo) {
    if (g_cmd_mode) { // 이미 명령어 모드일 때 Ctrl+C를 누르면 종료
        close_tui();
        client_log(LOG_INFO, "Program terminated by user (double Ctrl+C).");
        exit(0);
    }
    // 첫번째 Ctrl+C: 명령어 모드로 진입하기 위해 플래그 설정
    g_cmd_mode = 1;
    client_log(LOG_INFO, "EVENT: Ctrl+C detected, entering command mode.");
}


// --- 메인 함수 ---
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler); // SIGINT 핸들러 등록

    if (init_client_log() != 0) {
        // 로그 파일 열기 실패 시, 에러 메시지만 출력하고 종료
        exit(1);
    }
    client_log(LOG_INFO, "--- Client Started ---");

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "사용법: %s <Server IP> [Port]\n", argv[0]); 
        client_log(LOG_ERROR, "Invalid arguments: IP address and optional port required.");
        exit(1);
    }
    strncpy(g_server_ip, argv[1], 15); g_server_ip[15] = 0;

    int port = PORT;
    if (argc == 3) {
        int custom_port = atoi(argv[2]);
        if (custom_port > 0 && custom_port < 65536) {
            port = custom_port;
        } else {
            fprintf(stderr, "에러: 유효하지 않은 포트 번호입니다: %s\n", argv[2]);
            client_log(LOG_ERROR, "Invalid port number provided: %s", argv[2]);
            exit(1);
        }
    }
    
    client_log(LOG_INFO, "Attempting to connect to server: %s:%d", g_server_ip, port);
    struct sockaddr_in serv_addr;
    g_sock_main = socket(PF_INET, SOCK_STREAM, 0);
    if (g_sock_main == -1) {
        client_log(LOG_FATAL, "socket() error: %s", strerror(errno));
        handle_error("socket() 에러");
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(g_server_ip);
    serv_addr.sin_port = htons(port);
    if (connect(g_sock_main, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        client_log(LOG_FATAL, "connect() error: %s", strerror(errno));
        handle_error("connect() 에러");
    }
    
    client_log(LOG_INFO, "Connected to server (%s:%d). Starting authentication.", g_server_ip, port);
    printf("서버(%s:%d)에 연결됨. 인증을 시작합니다.\n", g_server_ip, port);
    
    if (!auth_client(g_sock_main)) {
        client_log(LOG_ERROR, "Authentication failed (user: %s)", g_user);
        printf("인증 실패.\n"); close(g_sock_main); 
        close_client_log();
        return 1;
    }

    client_log(LOG_INFO, "Authentication successful (user: %s). Starting TUI.", g_user);
    printf("인증 성공! TUI를 시작합니다.\n");
    sleep(1);
    init_tui();
    init_queue();
    init_debug_log();

    // 다운로드 진행상태 공유 변수 초기화
    pthread_mutex_init(&g_prog_mutex, NULL);
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        g_down_prog[i].active = 0;
        g_down_prog[i].progress = 0.0;
    }
    
    request_list(g_sock_main);
    g_scroll_time = time(NULL);
    while (1) {
        int needs_redraw = 0;

        if (g_cmd_mode) {
            enter_cmd_mode();
            g_cmd_mode = 0; // Reset flag after returning
            needs_redraw = 1;
        }

        int ch = getch();

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

        // 다운로드 중일 때 부드러운 진행률 표시를 위해 화면 강제 갱신
        int active_downloads = 0;
        pthread_mutex_lock(&g_prog_mutex);
        for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
            if (g_down_prog[i].active) {
                active_downloads = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_prog_mutex);

        if (active_downloads) {
            needs_redraw = 1;
        }

        if (needs_redraw) {
            draw_tui();
        }

        usleep(50000);
    }
    close_tui();
    close(g_sock_main);
    return 0;
}