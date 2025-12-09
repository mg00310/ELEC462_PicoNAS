#include "client.h"
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <byteswap.h>
#include <endian.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>          
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#ifndef htobe64
#define htobe64(x) __builtin_bswap64(x)
#endif
#ifndef be64toh
#define be64toh(x) __builtin_bswap64(x)
#endif

void get_hidden_input(char *buffer, size_t size);
static int recv_full(int sock, void* buf, size_t len);



// AUTH (로그인) 
int auth_client(int sock){
    printf("User: ");
    fgets(g_user, sizeof(g_user), stdin);
    g_user[strcspn(g_user,"\n")] = 0;

    printf("Pass: ");
    fflush(stdout);
    get_hidden_input(g_pass,sizeof(g_pass));   
    printf("\n");

    char sendbuf[200];
    snprintf(sendbuf,sizeof(sendbuf),"%s %s %s",CMD_AUTH,g_user,g_pass);
    send(sock,sendbuf,strlen(sendbuf),0);

    char resp[4];
    if(recv_full(sock,resp,4)<0) return -1;
    if(memcmp(resp,RESP_OK,4)!=0){
        printf("❌ 로그인 실패\n");
        return -1;
    }

    // 로그인 후 ROOT/CURR 경로 수신
    uint32_t len1,len2;

    recv_full(sock,&len1,4); len1=ntohl(len1);
    recv_full(sock,g_root_path,len1);
    g_root_path[len1]=0;

    recv_full(sock,&len2,4); len2=ntohl(len2);
    recv_full(sock,g_current_path,len2);
    g_current_path[len2]=0;

    printf("🔐 로그인 성공 → ROOT:%s\n", g_root_path);
    parse_path();
    return 1;
}



// 파일 목록 (LS)

void request_list(int sock){
    send(sock,CMD_LS,strlen(CMD_LS),0);

    char header[5]={0};
    recv_full(sock,header,4);
    if(strcmp(header,RESP_LS_S)!=0){
        strcpy(g_status_msg,"[ERR] 서버 응답 이상");
        return;
    }

    uint32_t cnt_net;
    recv_full(sock,&cnt_net,4);
    int count = ntohl(cnt_net);

    if(g_file_list) free(g_file_list);
    g_file_list = calloc(count,sizeof(struct FileInfo));
    g_file_count = count;

    for(int i=0;i<count;i++){
        recv_full(sock,&g_file_list[i].type,1);

        uint64_t s_net;
        recv_full(sock,&s_net,8);
        g_file_list[i].size = be64toh(s_net);

        uint32_t nl;
        recv_full(sock,&nl,4); nl = ntohl(nl);
        recv_full(sock,g_file_list[i].filename,nl);
        g_file_list[i].filename[nl]=0;

        // ⭐ 여기가 기존 코드에서 빠져있던 부분
        recv_full(sock, g_file_list[i].mod_time_str,20);
        g_file_list[i].mod_time_str[19]=0;

        recv_full(sock, g_file_list[i].owner,32);
        g_file_list[i].owner[31]=0;

        recv_full(sock, g_file_list[i].group,32);
        g_file_list[i].group[31]=0;

        recv_full(sock, g_file_list[i].permissions,12);
        g_file_list[i].permissions[11]=0;

        g_file_list[i].is_selected=0;
        g_file_list[i].is_downloaded=0;
    }

    char end[5]={0};
    recv_full(sock,end,4);  // ← LS_E 수신

    sort_list();
    strcpy(g_status_msg,"📁 목록 로드 완료");
}



// 디렉토리 이동 (CD)
void cd_client(int sock,const char* dirname){
    char buf[300];
    snprintf(buf,sizeof(buf),"%s %s\n",CMD_CD,dirname);
    send(sock,buf,strlen(buf),0);

    char resp[4];
    if(recv_full(sock,resp,4)<0) return;
    if(strcmp(resp,RESP_OK)!=0){
        strcpy(g_status_msg,"❌ 이동 실패");
        return;
    }

    uint32_t l;
    recv_full(sock,&l,4); l=ntohl(l);
    recv_full(sock,g_current_path,l);
    g_current_path[l]=0;

    parse_path();
    strcpy(g_status_msg,"📂 이동 완료");
}


//  CAT 파일 조회
char* cat_client_fetch(int sock,const char* filename,size_t* out_size){
    char buf[300];
    snprintf(buf,sizeof(buf),"%s %s\n",CMD_CAT,filename);
    send(sock,buf,strlen(buf),0);

    char resp[4];
    recv_full(sock,resp,4);
    if(strcmp(resp,RESP_OK)!=0) return NULL;

    size_t cap=8192,len=0;
    char* data=malloc(cap);

    while(1){
        char chunk[4096];
        int n=recv(sock,chunk,4096,0);
        if(n<=0){free(data);return NULL;}

        if(n>=4 && memcmp(chunk+n-4,RESP_CAT_E,4)==0){
            int real=n-4;
            if(len+real>cap){cap*=2; data=realloc(data,cap);}
            memcpy(data+len,chunk,real);
            len+=real;
            break;
        }

        if(len+n>cap){cap*=2;data=realloc(data,cap);}
        memcpy(data+len,chunk,n);
        len+=n;
    }

    *out_size=len;
    return data;
}



// 다운로드 스레드 (GET) 
void* download_thread(void* arg){
    struct DownloadArgs*A=(struct DownloadArgs*)arg;
    int slot=-1;

    pthread_mutex_lock(&g_prog_mutex);
    for(int i=0;i<MAX_ACTIVE_DOWNLOADS;i++){
        if(!g_down_prog[i].active){
            slot=i;
            g_down_prog[i].active=1;
            strncpy(g_down_prog[i].filename,A->file_info.filename,MAX_FILENAME);
            break;
        }
    }
    pthread_mutex_unlock(&g_prog_mutex);
    if(slot==-1){free(A);return NULL;}

    int sock=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    inet_pton(AF_INET,g_server_ip,&addr.sin_addr);
    connect(sock,(void*)&addr,sizeof(addr));

    // 서버에 파일 GET
    char cmd[300];
    snprintf(cmd,sizeof(cmd),"%s %s",CMD_GET,A->file_info.filename);
    send(sock,cmd,strlen(cmd),0);

    char resp[5]={0};
    recv_full(sock,resp,4);
    if(strcmp(resp,RESP_GET_S)!=0){
        g_down_prog[slot].active=0;
        close(sock); free(A);
        return NULL;
    }

    int64_t size_net;
    recv_full(sock,&size_net,8);
    long long total=be64toh(size_net);

    char local[500];
    snprintf(local,sizeof(local),"%s/%s",g_download_dir,A->file_info.filename);

    FILE*f=fopen(local,"wb");
    long rec=0;
    char buf[4096];

    while(rec<total){
        int n=recv(sock,buf,4096,0);
        if(n<=0)break;
        fwrite(buf,1,n,f);
        rec+=n;

        pthread_mutex_lock(&g_prog_mutex);
        g_down_prog[slot].progress=(double)rec/total;
        pthread_mutex_unlock(&g_prog_mutex);
    }

    fclose(f);
    close(sock);

    pthread_mutex_lock(&g_prog_mutex);
    g_down_prog[slot].active=0;
    pthread_mutex_unlock(&g_prog_mutex);

    add_queue(A->file_info.filename);
    free(A);
    return NULL;
}


//  다운로드 시작 (선택 파일 복수 처리)
void start_downloads(){
    for(int i=0;i<g_file_count;i++){
        if(g_file_list[i].is_selected){
            struct DownloadArgs*A=malloc(sizeof(struct DownloadArgs));
            A->file_info=g_file_list[i];
            strcpy(A->curr_path,g_current_path);

            pthread_t t;
            pthread_create(&t,NULL,download_thread,A);
            pthread_detach(t);

            g_file_list[i].is_selected=0;
        }
    }
    strcpy(g_status_msg,"⬇ 다운로드 시작");
}


void get_hidden_input(char *buffer, size_t size){
    struct termios old,new;
    tcgetattr(STDIN_FILENO,&old);
    new=old; new.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO,TCSANOW,&new);

    fgets(buffer,size,stdin);
    buffer[strcspn(buffer,"\n")]=0;

    tcsetattr(STDIN_FILENO,TCSANOW,&old);
}

static int recv_full(int sock, void* buf, size_t len){
    size_t now=0;
    while(now<len){
        ssize_t n=recv(sock,(char*)buf+now,len-now,0);
        if(n<=0) return -1;
        now+=n;
    }
    return 0;
}
