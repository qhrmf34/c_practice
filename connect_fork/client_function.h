#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H

#define BUF_SIZE 1024
#define MAX_THREADS 50  // 생성할 연결 스레드 개수

typedef struct {
    char *ip;
    int port;
    int thread_id;
} ThreadArg;

// 클라이언트 스레드 함수
void *client_thread(void *arg);

// 에러 처리 함수
void error_handling(char *message);

// 클라이언트 실행 함수
void client_run(int argc, char *argv[]);

#endif