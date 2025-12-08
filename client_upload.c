#include "client.h"
#include <dirent.h>
#include <sys/stat.h>

int ul_selected = 0;
char ul_current_path[1024] = ".";    // 시작 위치: 실행폴더
struct FileInfo *ul_list=NULL;
int ul_count=0;

void load_local_files(){
    DIR* dir = opendir(ul_current_path);
    struct dirent* ent;
    struct stat st;

    if(ul_list) free(ul_list);
    ul_count=0;

    while((ent=readdir(dir))){
        if(ent->d_name[0]=='.') continue;
        ul_count++;
    }
    rewinddir(dir);

    ul_list = malloc(sizeof(struct FileInfo)*ul_count);
    int i=0;
    while((ent=readdir(dir))){
        if(ent->d_name[0]=='.') continue;

        char full[1024];
        sprintf(full,"%s/%s",ul_current_path,ent->d_name);
        stat(full,&st);

        strcpy(ul_list[i].filename,ent->d_name);
        ul_list[i].type = S_ISDIR(st.st_mode)?'d':'f';
        ul_list[i].size = st.st_size;
        i++;
    }
    closedir(dir);
}

void draw_upload_ui(){
    erase();
    mvprintw(0,0,"📁 Local Upload Mode — %s",ul_current_path);
    mvprintw(1,0,"↑↓ 이동  Enter=업로드  ←=뒤로가기  Q=종료");

    for(int i=0;i<ul_count;i++){
        if(i==ul_selected) attron(COLOR_PAIR(7));

        mvprintw(i+3,0,"[%c] %s (%ldB)",
            ul_list[i].type=='d'?'D':'F',
            ul_list[i].filename,
            ul_list[i].size);

        if(i==ul_selected) attroff(COLOR_PAIR(7));
    }
    refresh();
}

void upload_file_to_server(const char* localpath,const char* servername){
    int fd=open(localpath,O_RDONLY);
    if(fd<0){ strcpy(g_status_msg,"파일 열기 실패"); return;}

    int sock=socket(PF_INET,SOCK_STREAM,0);
    struct sockaddr_in serv;
    serv.sin_family=AF_INET;
    serv.sin_port=htons(PORT);
    serv.sin_addr.s_addr=inet_addr(g_server_ip);

    connect(sock,(struct sockaddr*)&serv,sizeof(serv));

    char buf[2048],resp[5]={0};
    sprintf(buf,"%s %s %s",CMD_AUTH,g_user,g_pass);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    sprintf(buf,"%s %s",CMD_PUT,servername);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    if(strncmp(resp,RESP_PUT_S,4)!=0){
        strcpy(g_status_msg,"업로드 거부");
        close(fd);close(sock);return;
    }

    off_t size = lseek(fd,0,SEEK_END);
    lseek(fd,0,SEEK_SET);

    int64_t net = htobe64(size);
    write(sock,&net,8);

    ssize_t r; char fb[4096];
    while((r=read(fd,fb,4096))>0) write(sock,fb,r);

    read(sock,resp,4);
    if(strncmp(resp,RESP_PUT_E,4)==0)
        strcpy(g_status_msg,"✔ 업로드 완료");
    else
        strcpy(g_status_msg,"⛔ 실패");

    close(fd);close(sock);
    request_list(g_sock_main); // 서버 목록 갱신
}

void upload_mode(){
    load_local_files();

    while(1){
        draw_upload_ui();
        int ch=getch();

        if(ch=='q'||ch=='Q'){
            close_tui();
            exit(0);
        }
        if(ch==KEY_UP && ul_selected>0) ul_selected--;
        if(ch==KEY_DOWN && ul_selected<ul_count-1) ul_selected++;

        if(ch=='\n'||ch==10){ // 업로드 실행
            char full[1024];
            sprintf(full,"%s/%s",ul_current_path,ul_list[ul_selected].filename);

            if(ul_list[ul_selected].type=='d'){
                strcpy(g_status_msg,"폴더 업로드 미지원(파일만)");
            }else{
                upload_file_to_server(full,ul_list[ul_selected].filename);
                return; // 서버 UI로 복귀
            }
        }
        if(ch==KEY_LEFT){
            strcpy(g_status_msg,"서버 UI로 복귀");
            return;
        }
        if(ul_list[ul_selected].type=='d' && ch==KEY_RIGHT){
            sprintf(ul_current_path,"%s/%s",ul_current_path,ul_list[ul_selected].filename);
            ul_selected=0;
            load_local_files();
        }
    }
}
