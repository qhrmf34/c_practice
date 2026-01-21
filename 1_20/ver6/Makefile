CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = 

# 서버 소스 파일들
SERVER_SRCS = server_main.c server_run.c server_signal.c server_handler.c \
              server_accept.c server_worker.c server_child.c server_socket.c \
              server_log.c server_monitor.c

# 워커 소스 파일들 (child_process_main 공유)
WORKER_SRCS = worker_main.c server_child.c server_log.c server_monitor.c

# 클라이언트 소스 파일들
CLIENT_SRCS = client_main.c client_connect.c client_run.c

# 오브젝트 파일
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
WORKER_OBJS = $(WORKER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# 전체 빌드
all: server worker client

# 서버 실행 파일
server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 워커 실행 파일
worker: $(WORKER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 클라이언트 실행 파일
client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 서버 헤더 의존성
$(SERVER_OBJS): %.o: %.c server_function.h
	$(CC) $(CFLAGS) -c $< -o $@

# 워커 헤더 의존성
$(WORKER_OBJS): %.o: %.c server_function.h
	$(CC) $(CFLAGS) -c $< -o $@

# 클라이언트 헤더 의존성
$(CLIENT_OBJS): %.o: %.c client_function.h
	$(CC) $(CFLAGS) -c $< -o $@

# 정리
clean:
	rm -f *.o server worker client server.log

.PHONY: all clean
