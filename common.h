#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// --- 1. 서버 설정 ---
#define PORT 9999
#define MAX_FILENAME 256
#define MAX_NAME 32
#define MAX_PATH 4096

// --- 2. 프로토콜 명령어 (Client -> Server) ---
#define CMD_AUTH "AUTH" // + [user] [pass]
#define CMD_LS   "LS" // (현재 디렉터리 요청)
#define CMD_CD   "CD  " // + [dirname]
#define CMD_GET  "GET " // + [filename] (Phase 4에서 사용)
#define CMD_GETDIR "GDIR"
#define CMD_PUT "PUT "
#define RESP_GETDIR_S "GDIR"
#define RESP_PUT_S "PUTS_S"
#define RESP_PUT_E "PUTS_E"

// --- 3. 프로토콜 응답 (Server -> Client) ---
#define RESP_OK   "OK  "
#define RESP_ERR  "ERR "
#define RESP_LS_S "LS_S" // 'LS' 시작: + (uint32_t)file_count
#define RESP_LS_E "LS_E" // 'LS' 종료
#define RESP_GET_S "GET_S" // 'GET' 시작: + (int64_t)file_size

/*
 * AUTH, CD 응답 프로토콜
 * 성공 시: RESP_OK + (uint32_t)len + (char*)path
 */

// --- 4. 파일 정보 구조체 (프로토콜) ---
struct FileInfo {
    char filename[MAX_FILENAME];
    int64_t size;
    char type;
    
    int is_selected;
    int is_downloaded;

    char permissions[11];
    char owner[MAX_NAME];
    char group[MAX_NAME];
    char mod_time_str[20];
    int64_t mod_time_raw;
};

#endif //COMMON_H