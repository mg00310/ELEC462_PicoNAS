#include "client.h"
#include <wctype.h> // For iswspace, etc.
#include <locale.h> // For setlocale

void show_remote_file(const char* filename);

// --- TUI 그리기 ---
void scroll_text(int y, int x, const char* text, int max_width) {
    if (max_width <= 0) return;
    size_t len = get_mb_len(text);
    if (len <= max_width) {
        mvprintw(y, x, "%.*s", max_width, text);
        for(int i=len; i<max_width; i++) addch(' ');
        return;
    }
    int scroll_len = len + 3;
    int offset = g_scroll_tick % scroll_len;
    char scroll_buf[MAX_FILENAME * 2 + 4];
    snprintf(scroll_buf, sizeof(scroll_buf), "%s   %s", text, text);
    if (offset + max_width <= strlen(scroll_buf)) {
        mvprintw(y, x, "%.*s", max_width, scroll_buf + offset);
    } else {
        mvprintw(y, x, "%.*s", max_width, text);
    }
}

/**
 * @brief TUI 메인 화면을 그립니다.
 */
void draw_tui() {
    erase();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // --- 상단 바 (경로 또는 경로 탐색) ---
    int bg_pair_top;
    if (g_focus_zone == ZONE_PATH) {
        bg_pair_top = 10;
        attron(COLOR_PAIR(bg_pair_top));
        mvprintw(0, 0, "%*s", max_x, " ");
        mvprintw(0, 0, "경로 탐색: ");
        int current_x = 10;

        for (int i = 0; i < g_path_count; i++) {
            size_t seg_len = get_mb_len(g_path_segs[i]); 
            int display_len = seg_len + 4;
            if (current_x + display_len >= max_x) break;

            if (i == g_path_index) {
                attron(COLOR_PAIR(2));
                mvprintw(0, current_x, "[ %s ]", g_path_segs[i]);
                attroff(COLOR_PAIR(2));
            } else {
                attron(COLOR_PAIR(bg_pair_top));
                mvprintw(0, current_x, "  %s  ", g_path_segs[i]);
                attroff(COLOR_PAIR(bg_pair_top));
            }
            current_x += (seg_len + 4);
        }
    } else {
        bg_pair_top = 11; // 초록색 배경
        attron(COLOR_PAIR(bg_pair_top));
        mvprintw(0, 0, "%*s", max_x, " ");
        mvprintw(0, 0, "경로: %s", g_current_path);
    }
    attroff(COLOR_PAIR(bg_pair_top));
    

    // --- 상단 바 (정렬 상태) ---
    const char* base_modes[] = {"이름", "크기", "시간", "타입"};
    const char* order_str = (g_sort_order == 1) ? " ▲" : " ▼";
    
    const int max_sort_width = 6; 
    const int separator_width = 3; // " | "

    int total_sort_width = 6 + (max_sort_width * 4) + (separator_width * 3) + 1;
    
    int sort_x = max_x - total_sort_width;
    if (sort_x < 0) sort_x = 0;
    
    attron(COLOR_PAIR(bg_pair_top));
    mvprintw(0, sort_x, "[정렬: ");
    sort_x += 6;
    
    for (int i = 0; i < 4; i++) {
        char display_str[20];
        
        if (i == g_sort_mode) {
            snprintf(display_str, 20, "%s%s", base_modes[i], order_str);
        } else {
            strcpy(display_str, base_modes[i]);
        }
        
        size_t display_len = get_mb_len(display_str);
        
        if (i == g_sort_mode) {
            attron(COLOR_PAIR(2)); // 하이라이트
        } else {
            attron(COLOR_PAIR(bg_pair_top));
        }

        mvprintw(0, sort_x, "%s", display_str);
        for (int k = display_len; k < max_sort_width; k++) {
            addch(' ');
        }
        
        if (i == g_sort_mode) attroff(COLOR_PAIR(2));
        else attroff(COLOR_PAIR(bg_pair_top));

        if (i < 3) {
            attron(COLOR_PAIR(bg_pair_top));
            mvprintw(0, sort_x + max_sort_width, " | ");
            sort_x += (max_sort_width + separator_width);
        }
    }
    
    sort_x += max_sort_width;
    attron(COLOR_PAIR(bg_pair_top));
    mvprintw(0, sort_x, "]");
    attroff(COLOR_PAIR(bg_pair_top));


    // --- 헤더 ---
    if (g_focus_zone == ZONE_HEADER) attron(COLOR_PAIR(7));
    else attron(A_UNDERLINE | A_BOLD);
    
    int current_x = 0;
    int select_col_width = get_mb_len("[선택]") + 1;
    mvprintw(1, current_x, "[선택] "); current_x += select_col_width;
    
    int download_col_width = 10; // UI 수정 '완료' 컬럼 너비를 고정값으로 수정
    int optional_cols_width = 0;
    for (int i = 0; i < g_visible_cols; i++) {
        optional_cols_width += G_COL_WIDTHS[i];
    }
    
    int filename_width = max_x - select_col_width - optional_cols_width - download_col_width - 3;
    if (filename_width < 10) filename_width = 10;
    
    mvprintw(1, current_x, "| 파일명");
    for(int i=get_mb_len("파일명"); i < filename_width; i++) addch(' ');
    current_x += (filename_width + 1);
    
    for (int i = 0; i < g_visible_cols; i++) {
        mvprintw(1, current_x, "|%s", G_COL_NAMES_KR[i]);
        current_x += G_COL_WIDTHS[i];
    }
    mvprintw(1, current_x, " | [완료] ");
    
    if (g_focus_zone == ZONE_HEADER) attroff(COLOR_PAIR(7));
    else attroff(A_UNDERLINE | A_BOLD);

    // --- 파일 목록 ---
    int debug_panel_height = g_show_debug ? (DEBUG_MSG_COUNT + 2) : 0;
    int list_height = max_y - 3 - debug_panel_height;
    if (list_height < 1) list_height = 1;

    if (g_selected_item < g_scroll_offset) g_scroll_offset = g_selected_item;
    if (g_selected_item >= g_scroll_offset + list_height)
        g_scroll_offset = g_selected_item - list_height + 1;

    for (int i = g_scroll_offset; i < g_file_count && i < (g_scroll_offset + list_height) ; i++) {
        int screen_y = i - g_scroll_offset + 2;
        struct FileInfo *item = &g_file_list[i];
        
        /* --- 색상 표시 우선순위 ---
         * 1. 현재 커서 위치 (청록색 배경)
         * 2. 다운로드 선택 (빨간색 배경)
         * 3. 다운로드 완료 (파란 글씨/노란 배경)
         * 4. 상위 디렉터리 '..' (흐린 글씨)
         * 5. 디렉터리 (청록색 글씨)
         * 6. 심볼릭 링크 (자주색 글씨)
         * 7. 일반 파일 (흰색 글씨)
         */
        int color_pair;
        if (g_focus_zone == ZONE_LIST && i == g_selected_item) color_pair = 7; 
        else if (item->is_selected) color_pair = 5;
        else if (item->is_downloaded) color_pair = 12;
        else if (item->type == 'd' && strcmp(item->filename, "..") == 0) color_pair = 8;
        else if (item->type == 'd') color_pair = 3;
        else if (item->type == 'l') color_pair = 6;
        else color_pair = 1;
        
        attron(COLOR_PAIR(color_pair));
        if (color_pair == 8) attron(A_DIM);

        // 선택된 행 배경색 적용
        if (color_pair == 2) {
            mvprintw(screen_y, 0, "%*s", max_x - 1, " ");
            
        }

        current_x = 0;
        if (strcmp(item->filename, "..") == 0) {
             mvprintw(screen_y, current_x, "[ ]   ");

        } else {
             mvprintw(screen_y, current_x, "[%s]   ", item->is_selected ? "○" : " ");

        }

        current_x += select_col_width;
        
        mvprintw(screen_y, current_x, "| ");
        scroll_text(screen_y, current_x + 2, item->filename, filename_width);
        current_x += (filename_width + 1);

        // [UI Fix] 컬럼 너비에 맞게 출력 포맷을 조정하여 정렬 문제 해결
        for (int j = 0; j < g_visible_cols; j++) {
            switch(j) {
                case COL_TIME:
                    mvprintw(screen_y, current_x, "| %-16s", item->mod_time_str); break;
                case COL_SIZE:
                    if (item->type == 'd' || item->type == 'l' || item->size < 0) {
                        mvprintw(screen_y, current_x, "| %8s ", "");
                    } else {
                        char size_buf[10];
                        format_size(size_buf, 10, item->size);
                        mvprintw(screen_y, current_x, "| %8s", size_buf);
                    }
                    break;
                case COL_OWNER:
                    mvprintw(screen_y, current_x, "| %-8s", item->owner); break;
                case COL_GROUP:
                    mvprintw(screen_y, current_x, "| %-8s", item->group); break;
                case COL_PERM:
                    mvprintw(screen_y, current_x, "| %-11s", item->permissions); break;
            }
                    current_x += G_COL_WIDTHS[j];
                    }
                    mvprintw(screen_y, current_x, " | [ %s ]  ", item->is_downloaded ? "●" : " "); // UI 수정: 헤더와 너비를 맞추기 위해 공백 추가
                    
                            if (color_pair == 8) attroff(A_DIM);
                            attroff(COLOR_PAIR(color_pair));
                    
                            // 다운로드 진행률 막대를 화면에 덧그림
                            if (color_pair != 2) {
                                pthread_mutex_lock(&g_prog_mutex);
                                for (int j = 0; j < MAX_ACTIVE_DOWNLOADS; j++) {
                                    if (g_down_prog[j].active && strcmp(g_down_prog[j].filename, item->filename) == 0) {
                                        if (g_down_prog[j].progress > 0) {
                                            int bar_start_x = 0; // 줄의 시작부터
                                            int bar_width = (int)(g_down_prog[j].progress * (double)(max_x - 1)); // 전체 너비에 비례

                                            if (bar_width > max_x - 1) bar_width = max_x - 1;

                                            if (bar_width > 0) {
                                                mvwchgat(stdscr, screen_y, bar_start_x, bar_width, 0, 2, NULL);
                                            }
                    
                                        }
                    
                                        break;
                                    }
                    
                                }
                                pthread_mutex_unlock(&g_prog_mutex);
                            }
    }

    int status_bar_y = max_y - 1 - debug_panel_height;

    // --- 하단 상태 표시줄 ---
    attron(COLOR_PAIR(11));
    mvprintw(status_bar_y, 0, "%*s", max_x, " ");

    char help_str[256];
    switch(g_focus_zone) {
        case ZONE_PATH:
            snprintf(help_str, 256, "[↔]이동 [Enter]선택 [↓]헤더 [Q]종료");
            break;
        case ZONE_HEADER:
            snprintf(help_str, 256, "[↔]컬럼 [C]정렬기준 [D]정렬순서 [⤉]경로 [↓]목록 [Q]종료");
            break;
        case ZONE_LIST:
        default:
             if (g_file_count > 0) {
                char enter_action[20];
                if (g_file_list[g_selected_item].type == 'd') {
                    snprintf(enter_action, sizeof(enter_action), "이동");
                } else if (g_file_list[g_selected_item].type == 'f') {
                    if (is_binary(g_file_list[g_selected_item].filename)) {
                        snprintf(enter_action, sizeof(enter_action), "미리보기 불가");
                    } else {
                        snprintf(enter_action, sizeof(enter_action), "보기");
                    }
                } else {
                    snprintf(enter_action, sizeof(enter_action), "동작불가");
                }
                snprintf(help_str, 256, "[↑↓]이동 [Space]선택 [Enter]%s [D]다운 [Q]종료", enter_action);
            } else {
                snprintf(help_str, 256, "[↑↓]이동 [Space]선택 [Enter] [D]다운 [Q]종료");
            }
            break;
    }
    
    char status_bar_text[max_x + 1];
    snprintf(status_bar_text, sizeof(status_bar_text), " %s | %s", help_str, g_status_msg);
    
    mvprintw(status_bar_y, 0, "%.*s", max_x, status_bar_text);
    attroff(COLOR_PAIR(11));

    draw_debug_log(max_y, max_x);

    refresh();
}

/**
 * @brief 키 입력을 처리합니다.
 */
void handle_keys(int ch) {
    if (g_show_debug) {
        clear_debug_log();
        add_debug_log("KEY: %s", get_key_str(ch));
    }
    client_log(LOG_DEBUG, "Key press handled: %s", get_key_str(ch));
    g_status_msg[0] = '\0';
    switch (g_focus_zone) {
        case ZONE_PATH:
            switch (ch) {
                case 'q': case 'Q': close_tui(); exit(0); break;
                case KEY_DOWN: g_focus_zone = ZONE_HEADER; break;
                
                case KEY_LEFT:
                    if (g_path_index > 0) g_path_index--;
                    break;
                case KEY_RIGHT:
                    if (g_path_index < g_path_count - 1) g_path_index++;
                    break;
                
                case KEY_ENTER: case 10:
                    if (g_path_index >= 0 && g_path_index < g_path_count) {
                        
                        char* target_path = g_path_routes[g_path_index];

                        if (target_path) {
                            if (strcmp(target_path, g_current_path) == 0) {
                                snprintf(g_status_msg, 100, "이미 현재 경로입니다.");
                            } else {
                                cd_client(g_sock_main, target_path);
                            }
                        } else {
                            snprintf(g_status_msg, 100, "경로 인덱스 오류");
                        }
                        
                        g_focus_zone = ZONE_LIST;

                    } else {
                         snprintf(g_status_msg, 100, "세그먼트 인덱스 오류");
                    }
                    break;
            }
            break;
        
        case ZONE_HEADER:
            switch (ch) {
                case 'q': case 'Q': close_tui(); exit(0); break;
                case KEY_UP:
                    parse_path();
                    g_focus_zone = ZONE_PATH;
                    break;
                case KEY_DOWN: g_focus_zone = ZONE_LIST; break;
                case KEY_RIGHT:
                    if (g_visible_cols < NUM_OPT_COLS) g_visible_cols++;
                    break;
                case KEY_LEFT:
                    if (g_visible_cols > 0) g_visible_cols--;
                    break;
                case 'c': case 'C':
                    g_sort_mode = (g_sort_mode + 1) % 4;
                    sort_list();
                    break;
                case 'd': case 'D':
                    g_sort_order *= -1;
                    sort_list();
                    break;
            }
            break;
        case ZONE_LIST:
            switch (ch) {
                case 'q': case 'Q': close_tui(); exit(0); break;
                case KEY_UP:
                    if (g_selected_item > 0) g_selected_item--;
                    else g_focus_zone = ZONE_HEADER;
                    break;
                case KEY_DOWN:
                    if (g_selected_item < g_file_count - 1) g_selected_item++;
                    break;
                case KEY_PPAGE: g_selected_item -= 10; if (g_selected_item < 0) g_selected_item = 0; break;
                case KEY_NPAGE: g_selected_item += 10; if (g_selected_item >= g_file_count) g_selected_item = g_file_count - 1; break;
                case KEY_ENTER: case 10:
                    if (g_file_list[g_selected_item].type == 'd') {
                        cd_client(g_sock_main, g_file_list[g_selected_item].filename);
                    } else if (g_file_list[g_selected_item].type == 'f') {
                        if (is_binary(g_file_list[g_selected_item].filename)) {
                             snprintf(g_status_msg, 100, "미리보기 불가");
                        } else {
                            show_remote_file(g_file_list[g_selected_item].filename);
                        }
                    } else {
                        snprintf(g_status_msg, 100, "디렉터리 또는 파일이 아닙니다.");
                    }
                    break;
                case KEY_BACKSPACE: // 부모 디렉터리로 이동
                    cd_client(g_sock_main, "..");
                    break;
                case ' ':
                    if (strcmp(g_file_list[g_selected_item].filename, "..") != 0) {
                        g_file_list[g_selected_item].is_selected = !g_file_list[g_selected_item].is_selected;
                    }
                    break;
                case 'D': case 'd':
                    start_downloads();
                    break;
            }
            break;
    }
}

// Private helper function to display content in a scrollable viewer
void show_content_viewer(const char* title, const char* content) {
    if (!content) {
        snprintf(g_status_msg, 100, "내용이 없습니다.");
        return;
    }
    
    // 파일 내용을 줄 단위로 분리
    char** lines = NULL;
    int line_count = 0;
    char* content_copy = strdup(content);
    if (!content_copy) {
        snprintf(g_status_msg, 100, "메모리 할당 실패.");
        return;
    }

    char* line = strtok(content_copy, "\n");
    while (line) {
        lines = (char**)realloc(lines, (line_count + 1) * sizeof(char*));
        if (!lines) {
            snprintf(g_status_msg, 100, "메모리 할당 실패.");
            free(content_copy);
            return;
        }
        lines[line_count++] = line;
        line = strtok(NULL, "\n");
    }

    int max_y, max_x;
    int scroll_pos = 0;
    int ch;

    while (1) {
        getmaxyx(stdscr, max_y, max_x);
        int content_height = max_y - 2;
        erase();

        attron(COLOR_PAIR(11));
        mvprintw(0, 0, "%*s", max_x, " ");
        mvprintw(0, 0, "%s", title);
        attroff(COLOR_PAIR(11));

        for (int i = 0; i < content_height; i++) {
            if (scroll_pos + i < line_count) {
                mvprintw(i + 1, 0, "%.*s", max_x, lines[scroll_pos + i]);
            }
        }

        attron(COLOR_PAIR(11));
        mvprintw(max_y - 1, 0, "%*s", max_x, " ");
        mvprintw(max_y - 1, 0, "[↑↓]스크롤 [Enter/Q/ESC]나가기"); // Updated help text
        attroff(COLOR_PAIR(11));

        refresh();
        ch = getch();

        if (ch == KEY_UP && scroll_pos > 0) scroll_pos--;
        else if (ch == KEY_DOWN && scroll_pos + content_height < line_count) scroll_pos++;
        else if (ch == '\n' || ch == KEY_ENTER || ch == 'q' || ch == 'Q' || ch == 27) break; // Added 'ch == 27' for ESC
    }

    free(content_copy);
    if (lines) free(lines);
}

void show_remote_file(const char* filename) {
    size_t content_size = 0;
    char* content = cat_client_fetch(g_sock_main, filename, &content_size);
    if (!content) {
        snprintf(g_status_msg, 100, "파일 내용을 불러오지 못했습니다.");
        return;
    }
    
    char title[100];
    snprintf(title, sizeof(title), "파일 미리보기: %s", filename);
    show_content_viewer(title, content);
    free(content);
}

void show_local_file(const char* local_path) {
    FILE* fp = fopen(local_path, "r");
    if (!fp) {
        snprintf(g_status_msg, 100, "로그 파일을 열 수 없습니다: %s", local_path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = malloc(fsize + 1);
    if (!content) {
        fclose(fp);
        snprintf(g_status_msg, 100, "메모리 할당 실패.");
        return;
    }
    
    fread(content, 1, fsize, fp);
    fclose(fp);
    content[fsize] = 0;

    char title[100];
    snprintf(title, sizeof(title), "로그 파일: %s", local_path);
    show_content_viewer(title, content);
    free(content);
}

