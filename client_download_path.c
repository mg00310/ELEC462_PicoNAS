#include "client.h"
#include <dirent.h>
#include <sys/stat.h>

int dp_sel = 0;
char dp_path[1024] = ".";

// 로컬 폴더 불러오기
void dp_load() {
    DIR* d = opendir(dp_path);
    struct dirent* e;
    static struct FileInfo *list=NULL;

    free(list); list=NULL;
    int c=0;

    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;
        c++;
    }
    rewinddir(d);

    list = malloc(sizeof(struct FileInfo)*c);
    int i=0;
    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;
        strcpy(list[i].filename,e->d_name);
        i++;
    }
    closedir(d);

    erase();
    mvprintw(0,0,"📂 Download Path Select Mode");
    mvprintw(1,0,"↑↓ 이동  → 폴더 진입  ← 취소  Enter=이 경로로 다운로드 저장 폴더 설정");

    for(int j=0;j<c;j++){
        if(j==dp_sel) attron(COLOR_PAIR(7));
        mvprintw(j+3,0,"%s/",list[j].filename);
        if(j==dp_sel) attroff(COLOR_PAIR(7));
    }
    refresh();
}

// 실행 UI
void download_path_mode(){
    dp_sel=0;
    strcpy(dp_path,".");

    while(1){
        dp_load();
        int ch=getch();

        if(ch=='q'||ch=='Q') return;
        if(ch==KEY_UP && dp_sel>0) dp_sel--;
        if(ch==KEY_DOWN) dp_sel++;

        if(ch==KEY_LEFT) return;

        if(ch==10){ // Enter = 경로 선택 저장
            realpath(dp_path,g_download_dir);
            snprintf(g_status_msg,100,"✔ Download save path: %s",g_download_dir);
            return;
        }

        if(ch==KEY_RIGHT){
            strcat(dp_path,"/");
            // 진입
        }
    }
}
