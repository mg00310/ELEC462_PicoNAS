// ================= client_download_path.c =================
#include "client.h"
#include <dirent.h>
#include <sys/stat.h>
 
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
