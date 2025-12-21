#include "client.h"
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

static FILE* log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
const char* level_strings[] = {"INFO", "DEBUG", "WARN", "ERROR", "FATAL"};
 
/**
 * @brief 클라이언트 로그 파일을 생성하고 엽니다.
 * 'logs' 디렉터리를 만들고, 타임스탬프 기반의 로그 파일을 생성합니다.
 * @return 성공 시 0, 실패 시 -1.
 */
int init_client_log() {
    struct stat st = {0};
    // 'logs' 디렉터리가 없으면 생성
    if (stat("logs", &st) == -1) {
        mkdir("logs", 0755);
    }

    char filepath[100];
    char time_str[20];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d_%H-%M-%S", &lt);

    snprintf(filepath, sizeof(filepath), "logs/client_%s.log", time_str);

    log_file = fopen(filepath, "a");
    if (!log_file) {
        perror("클라이언트 로그 파일 열기 실패");
        return -1;
    }
    return 0;
}

/**
 * @brief 클라이언트 로그 파일을 닫습니다.
 */
void close_client_log() {
    if (log_file) {
        fclose(log_file);
    }
}

/**
 * @brief 스레드에 안전한 클라이언트 로깅 함수 (타임스탬프, 로그레벨 포함).
 * @param level 로그 레벨.
 * @param format printf와 동일한 형식의 문자열.
 * @param ... 가변 인자.
 */
void client_log(int level, const char *format, ...) {
    if (level < g_debug_level) {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    if (!log_file) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    char time_str[20];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &lt);

    fprintf(log_file, "[%s] [%-5s] ", time_str, level_strings[level]);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);

    pthread_mutex_unlock(&log_mutex);
}
