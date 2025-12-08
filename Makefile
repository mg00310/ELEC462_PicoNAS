# Makefile for Pico NAS Project (고급 기능 버전)

# 컴파일러
CC = gcc
# 경고 제거 옵션 추가
CFLAGS = -Wall -g \
    -Wno-format-truncation -Wno-stringop-overflow -Wno-unused-result \
    -Wno-stringop-overread -Wno-format-overflow


# 서버 관련
SERVER_SRC = server.c
SERVER_BIN = server
SERVER_LIBS = -pthread

# 클라이언트 관련
CLIENT_SRC = client_main.c client_ui.c client_net.c client_utils.c client_upload.c client_download_path.c
CLIENT_BIN = client
CLIENT_LIBS = -lncursesw -pthread

# 기본 타겟
all: $(SERVER_BIN) $(CLIENT_BIN)

# 서버 빌드
$(SERVER_BIN): $(SERVER_SRC) common.h
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(SERVER_LIBS)

# 클라이언트 빌드
$(CLIENT_BIN): $(CLIENT_SRC) common.h client.h
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC) $(CLIENT_LIBS)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) *.o

.PHONY: all clean
