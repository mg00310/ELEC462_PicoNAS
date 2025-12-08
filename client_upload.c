#include "client.h"
#include <unistd.h>
#include <limits.h>   // realpath PATH_MAX 필요
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h> // dirname 함수를 사용하기 위해 추가
#include <errno.h> // 에러 메시지 처리를 위해 추가
#include <byteswap.h>

#include <endian.h>
#ifndef htobe64
#define htobe64(x) __builtin_bswap64(x)
#endif
#ifndef be64toh
#define be64toh(x) __builtin_bswap64(x)
#endif

int ul_selected = 0;
char ul_current_path[1024] = "/";
struct FileInfo *ul_list=NULL;
int ul_count=0;

/**
 * @brief 현재 로컬 경로의 파일/폴더 목록을 불러옵니다.
 */
void load_local_files(){
    DIR* dir = opendir(ul_current_path);
    struct dirent* ent;
    struct stat st;

    if(ul_list) free(ul_list);
    ul_list=NULL;
    ul_count=0;

    // 경로 접근 실패 시 처리
    if (!dir) {
        char temp_path[1024];
        strcpy(temp_path, ul_current_path);
        
        // 부모 경로로 이동 시도
        char *parent = dirname(temp_path);
        if (parent && strcmp(parent, ul_current_path) != 0) {
            strncpy(ul_current_path, parent, 1024);
            ul_current_path[1023] = '\0';
            dir = opendir(ul_current_path); // 새로운 경로로 다시 열기 시도
        }
    }
    
    if (!dir) { 
        // 최종적으로도 폴더를 열 수 없다면, 파일 목록을 비우고 종료
        snprintf(g_status_msg, 100, "⛔ 로컬 폴더 접근 실패 (%s)", strerror(errno));
        return; 
    }

    // 파일 개수 카운트
    int dot_dot = (strcmp(ul_current_path, "/") != 0); // 루트가 아니면 ".." 필요
    ul_count = dot_dot ? 1 : 0; 
    
    while((ent=readdir(dir))){
        if(ent->d_name[0]=='.') continue;
        ul_count++;
    }
    rewinddir(dir);

    ul_list = malloc(sizeof(struct FileInfo)*ul_count);
    if (!ul_list) { closedir(dir); return; }

    int i=0;
    
    // ".." 항목 추가 (루트가 아닐 경우)
    if (dot_dot) {
        strcpy(ul_list[i].filename, "..");
        ul_list[i].type = 'd';
        ul_list[i].size = 0;
        i++;
    }

    // 실제 파일 목록 채우기
    while((ent=readdir(dir))){
        if(ent->d_name[0]=='.') continue;
        
        char full[1024];
        // 경로 구성 시 중복 슬래시 방지
        if (ul_current_path[strlen(ul_current_path)-1] == '/') {
            sprintf(full,"%s%s", ul_current_path, ent->d_name);
        } else {
            sprintf(full,"%s/%s", ul_current_path, ent->d_name);
        }
        
        stat(full,&st);

        strcpy(ul_list[i].filename,ent->d_name);
        ul_list[i].type = S_ISDIR(st.st_mode)?'d':'f';
        ul_list[i].size = st.st_size;
        i++;
    }
    closedir(dir);
    ul_count = i;
    
    // 선택 범위 보정
    if (ul_selected >= ul_count) ul_selected = ul_count > 0 ? ul_count - 1 : 0;
    if (ul_selected < 0) ul_selected = 0;
}

/**
 * @brief 업로드 UI를 그립니다.
 */
void draw_upload_ui(){
    erase();
    attron(COLOR_PAIR(10));
    mvprintw(0,0,"📁 Local Upload Mode — %s",ul_current_path);
    attroff(COLOR_PAIR(10));
    mvprintw(1,0,"↑↓ 이동  Enter=업로드  ←=뒤로가기  →=폴더진입  Q=종료");

    for(int i=0;i<ul_count;i++){
        if(i==ul_selected) attron(COLOR_PAIR(7));

        int color_pair = (ul_list[i].type == 'd' || strcmp(ul_list[i].filename, "..") == 0) ? 3 : 1;
        if(i==ul_selected) color_pair = 7;
        attron(COLOR_PAIR(color_pair));

        // 파일 크기 포맷
        char size_str[20];
        if (ul_list[i].type == 'f') {
            format_size(size_str, sizeof(size_str), ul_list[i].size);
        } else {
            strcpy(size_str, "");
        }
        
        mvprintw(i+3,0,"[%c] %-40s %s",
            ul_list[i].type=='d'?'D':'F',
            ul_list[i].filename,
            size_str);

        attroff(COLOR_PAIR(color_pair));
    }
    refresh();
}

/**
 * @brief 서버로 파일을 전송합니다. (버그 수정됨)
 */
void upload_file_to_server(const char* localpath,const char* servername){
    int fd=open(localpath,O_RDONLY);
    if(fd<0){ snprintf(g_status_msg,100,"파일 열기 실패: %s",strerror(errno)); return;}

    int sock=socket(PF_INET,SOCK_STREAM,0);
    if (sock < 0) { close(fd); strcpy(g_status_msg, "소켓 생성 실패"); return; }
    
    struct sockaddr_in serv;
    serv.sin_family=AF_INET;
    serv.sin_port=htons(PORT);
    serv.sin_addr.s_addr=inet_addr(g_server_ip);

    if (connect(sock,(struct sockaddr*)&serv,sizeof(serv)) < 0) {
        close(fd); close(sock); snprintf(g_status_msg, 100, "서버 연결 실패 (%s)", strerror(errno)); return;
    }

    char buf[2048],resp[5]={0};
    sprintf(buf,"%s %s %s",CMD_AUTH,g_user,g_pass);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    if (strncmp(resp, RESP_OK, 4) != 0) {
        close(fd); close(sock); strcpy(g_status_msg, "인증 실패 (업로드 불가)"); return;
    }

    // 1. 파일 크기 계산 및 포인터 리셋 (버그 수정: lseek(SEEK_SET) 추가)
    off_t size = lseek(fd,0,SEEK_END);
    lseek(fd,0,SEEK_SET); 

    // 2. PUT 명령 전송
    sprintf(buf,"%s %s",CMD_PUT,servername);
    write(sock,buf,strlen(buf));
    read(sock,resp,4);

    if(strncmp(resp,RESP_PUT_S,4)!=0){
        snprintf(g_status_msg, 100, "업로드 거부 (응답: %.4s)", resp);
        close(fd);close(sock);return;
    }

    // 3. 파일 크기 전송
    int64_t net = htobe64(size);
    write(sock,&net,8);

    // 4. 파일 내용 전송
    ssize_t r; char fb[4096];
    while((r=read(fd,fb,4096))>0) write(sock,fb,r);

    // 5. 완료 응답 수신
    read(sock,resp,4);
    if(strncmp(resp,RESP_PUT_E,4)==0)
        strcpy(g_status_msg,"✔ 업로드 완료");
    else
        snprintf(g_status_msg, 100, "⛔ 실패 (응답: %.4s)", resp);

    close(fd);close(sock);
    request_list(g_sock_main); // 서버 목록 갱신
}

/**
 * @brief 로컬 파일 탐색 및 업로드 모드 메인 루프 (경로 탐색 UI 추가)
 */
void upload_mode(){
    if (ul_list) {
        free(ul_list);
        ul_list = NULL;
    }
    
    // 초기 경로 설정 (루트에서 시작)
    if (strcmp(ul_current_path, ".") == 0) {
         strcpy(ul_current_path, "/"); 
    }
    ul_selected = 0;
    
    // 노딜레이 모드 임시 해제
    nodelay(stdscr, FALSE);
    
    load_local_files();

    while(1){
        draw_upload_ui();
        int ch=getch();
        
        if (ul_list == NULL && ul_count == 0) { // 목록 로드 실패 시 탈출
            break;
        }

        if(ch=='q'||ch=='Q'){
            close_tui();
            exit(0);
        }
        
        // 선택 범위 제어
        if(ch==KEY_UP && ul_selected>0) ul_selected--;
        if(ch==KEY_DOWN && ul_selected<ul_count-1) ul_selected++;

        if(ch=='\n'||ch==10){ // Enter: 업로드 실행 또는 폴더 진입
            char full[1024];
            
            // ".." 항목을 Enter로 누르면 진입 (KEY_RIGHT와 동일 동작)
            if (ul_list[ul_selected].type=='d') goto KEY_RIGHT_ACTION;

            snprintf(full, sizeof(full), "%s/%s", ul_current_path, ul_list[ul_selected].filename);

            if(ul_list[ul_selected].type=='d'){
                strcpy(g_status_msg,"폴더 업로드 미지원 (파일만 가능)");
            } else {
                // 노딜레이 복구
                nodelay(stdscr, TRUE);
                upload_file_to_server(full, ul_list[ul_selected].filename);
                return; // 서버 UI로 복귀
            }
        }
        
        if(ch==KEY_LEFT){ // ← : 서버 UI로 복귀 (취소)
            strcpy(g_status_msg,"업로드 모드 취소. 서버 UI로 복귀");
            break;
        }
        
        if(ch==KEY_RIGHT){
            KEY_RIGHT_ACTION:; // goto 레이블
            if(ul_list[ul_selected].type=='d'){
                char selected_name[MAX_FILENAME];
                strcpy(selected_name, ul_list[ul_selected].filename);
                
                if (strcmp(selected_name, "..") == 0) {
                    // ".." 이면 부모 경로로 이동
                    char temp[1024];
                    strcpy(temp, ul_current_path);
                    char *parent_path = dirname(temp);

                    if (parent_path && strcmp(parent_path, ul_current_path) != 0) {
                        strncpy(ul_current_path, parent_path, 1024);
                        // 루트가 아니면 마지막 '/' 제거
                        if (strcmp(ul_current_path, "/") != 0 && ul_current_path[strlen(ul_current_path) - 1] == '/') {
                             ul_current_path[strlen(ul_current_path) - 1] = '\0';
                        }
                    } else if (strcmp(ul_current_path, "/") != 0) {
                        // dirname이 현재 경로와 같고 루트가 아니라면, 강제로 루트로 이동
                        strcpy(ul_current_path, "/");
                    }
                } else {
                    // 일반 폴더 진입
                    char new_path[1024];
                    snprintf(new_path, sizeof(new_path), "%s/%s", ul_current_path, selected_name);
                    
                    // 불필요한 이중 슬래시 제거 로직 (경로 정규화)
                    char temp_path[1024];
                    int k=0;
                    for(int j=0; new_path[j] != '\0'; j++) {
                        if (new_path[j] == '/' && new_path[j+1] == '/' && j > 0) continue;
                        temp_path[k++] = new_path[j];
                    }
                    temp_path[k] = '\0';
                    
                    // 루트 경로가 아닌데 뒤에 /가 남아있으면 제거 (dirname()의 동작 방식 맞춤)
                    if (strcmp(temp_path, "/") != 0 && temp_path[strlen(temp_path)-1] == '/') {
                        temp_path[strlen(temp_path)-1] = '\0';
                    }
                    
                    strncpy(ul_current_path, temp_path, 1024);
                    ul_current_path[1023] = '\0';
                }

                ul_selected=0;
                load_local_files();
            }
        }
    }
    
    if (ul_list) {
        free(ul_list);
        ul_list = NULL;
    }
    // 노딜레이 복구
    nodelay(stdscr, TRUE);
}