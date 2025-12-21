// ================= client_download_path.c =================
#include "client.h"
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

char g_local_download_path[1024] = "./downloads";

void draw_path_select_ui(const char* current_path, int selected, struct FileInfo* list, int count) {
    erase();
    attron(COLOR_PAIR(10));
    mvprintw(0, 0, "📍 다운로드 경로 설정 — %s", current_path);
    attroff(COLOR_PAIR(10));
    mvprintw(1, 0, "↑↓ 이동  Enter: 폴더진입  S: 현재 폴더로 설정  ←: 취소");

    for (int i = 0; i < count; i++) {
        int color = (i == selected) ? 7 : (list[i].type == 'd' ? 3 : 1);
        attron(COLOR_PAIR(color));
        mvprintw(i + 3, 0, "[%c] %-40s", list[i].type == 'd' ? 'D' : 'F', list[i].filename);
        attroff(COLOR_PAIR(color));
    }
    refresh();
}

void select_download_path() {
    char cur_path[1024];
    getcwd(cur_path, sizeof(cur_path)); // 현재 실행 경로에서 시작
    
    int selected = 0;
    nodelay(stdscr, FALSE); // 입력 대기 모드

    while (1) {
        // 1. 로컬 파일 목록 로드 
        DIR* dir = opendir(cur_path);
        if (!dir) break;

        struct FileInfo temp_list[256];
        int count = 0;
        
        strcpy(temp_list[count].filename, "..");
        temp_list[count++].type = 'd';

        struct dirent* ent;
        while ((ent = readdir(dir)) && count < 256) {
            if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
            if (ent->d_name[0] == '.') continue;
            
            struct stat st;
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", cur_path, ent->d_name);
            if (stat(full, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    strcpy(temp_list[count].filename, ent->d_name);
                    temp_list[count].type = 'd';
                    temp_list[count].size = 0; // 폴더는 크기 표시 필요 없음
                    count++;
                }
            }
            
            if (S_ISDIR(st.st_mode)) { // 폴더만 리스트에 넣음
                strcpy(temp_list[count].filename, ent->d_name);
                temp_list[count++].type = 'd';
            }
        }
        closedir(dir);

        draw_path_select_ui(cur_path, selected, temp_list, count);
        int ch = getch();

        if (ch == KEY_UP && selected > 0) selected--;
        if (ch == KEY_DOWN && selected < count - 1) selected++;
        if (ch == KEY_LEFT) break; // 취소

        if (ch == '\n' || ch == 10) { // 폴더 진입
            if (strcmp(temp_list[selected].filename, "..") == 0) {
                char* parent = dirname(cur_path);
                strcpy(cur_path, parent);
            } else {
                strcat(cur_path, "/");
                strcat(cur_path, temp_list[selected].filename);
            }
            selected = 0;
        }

        if (ch == 's' || ch == 'S') { // 현재 경로 확정
            strcpy(g_local_download_path, cur_path);
            snprintf(g_status_msg, 100, "✔ 다운로드 경로 변경됨: %s", g_local_download_path);
            break;
        }
    }
    nodelay(stdscr, TRUE);
}

int dp_sel = 0;
char dp_path[1024] = ".";
 
struct FileInfo *dp_list = NULL;
int dp_count = 0;

// 현재 폴더 목록 읽기
void dp_refresh(){
    DIR *d = opendir(dp_path);
    struct dirent *e;
    struct stat st;

    if(dp_list) { free(dp_list); dp_list=NULL; }
    dp_count=0;

    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;
        dp_count++;
    }
    rewinddir(d);

    dp_list = malloc(sizeof(struct FileInfo)*dp_count);
    int i=0;

    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;

        char full[1024];
        sprintf(full,"%s/%s",dp_path,e->d_name);
        stat(full,&st);

        strcpy(dp_list[i].filename,e->d_name);
        dp_list[i].type = S_ISDIR(st.st_mode)?'d':'f';
        i++;
    }
    closedir(d);
}

// UI 표시
void dp_draw(){
    erase();
    mvprintw(0,0,"📂 Download Path Select Mode");
    mvprintw(1,0,"↑↓ 선택  ← 상위폴더  Enter:폴더열기  SPACE:현재 폴더 선택(Q=취소)");
    mvprintw(2,0,"📁 현재 경로: %s",dp_path);

    for(int i=0;i<dp_count;i++){
        if(i==dp_sel) attron(COLOR_PAIR(7));
        mvprintw(i+4,0,"[%c] %s",
            dp_list[i].type=='d'?'D':'F',
            dp_list[i].filename);
        if(i==dp_sel) attroff(COLOR_PAIR(7));
    }
    refresh();
}

// 실행
void download_path_mode(){
    dp_sel=0;
    dp_refresh();

    while(1){
        dp_draw();
        int ch=getch();

        if(ch=='q'||ch=='Q') return;

        if(ch==KEY_UP && dp_sel>0) dp_sel--;
        if(ch==KEY_DOWN && dp_sel<dp_count-1) dp_sel++;

        // 폴더 선택 후 Space → 다운로드 폴더 확정
        if(ch==' '){
            realpath(dp_path,g_download_dir);
            snprintf(g_status_msg,99,"✔ 저장 위치: %s",g_download_dir);
            return;
        }

        // Enter → 폴더 진입
        if(ch==10 && dp_list[dp_sel].type=='d'){
            strcat(dp_path,"/");
            strcat(dp_path,dp_list[dp_sel].filename);
            dp_sel=0;
            dp_refresh();
        }

        // ← → 상위 디렉토리
        if(ch==KEY_LEFT){
            char *p = strrchr(dp_path,'/');
            if(p && p!=dp_path) *p=0;
            else strcpy(dp_path,".");
            dp_sel=0;
            dp_refresh();
        }
    }
}
