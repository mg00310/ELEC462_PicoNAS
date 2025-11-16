# Makefile for Pico NAS Project (고급 기능 버전)

# 컴파일러
CC = gcc
# 공통 C 플래그 (경고 표시, 디버깅 정보 포함)
CFLAGS = -Wall -g

# 서버 관련
SERVER_SRC = server.c
SERVER_BIN = server
# [수정 없음] 서버 라이브러리 (스레드)
SERVER_LIBS = -pthread

# 클라이언트 관련
CLIENT_SRC = client_main.c client_ui.c client_net.c client_utils.c
CLIENT_BIN = client
# [수정] 클라이언트 라이브러리 (ncurses와 pthread 모두 필요)
CLIENT_LIBS = -lncursesw -pthread

# 기본 타겟: all (서버와 클라이언트 모두 빌드)
all: $(SERVER_BIN) $(CLIENT_BIN)

# 서버 빌드 규칙
$(SERVER_BIN): $(SERVER_SRC) common.h
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(SERVER_LIBS)

# 클라이언트 빌드 규칙
$(CLIENT_BIN): $(CLIENT_SRC) common.h client.h
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC) $(CLIENT_LIBS)

# 정리
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) *.o

.PHONY: all clean