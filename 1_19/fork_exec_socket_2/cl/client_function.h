#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H

#include <arpa/inet.h>
#include <signal.h>

#define BUF_SIZE 1024
#define IO_COUNT 10  // 각 클라이언트가 수행할 IO 횟수

// 클라이언트 실행 함수 (blocking)
void 
client_run(const char *ip, int port, int client_id);

extern volatile sig_atomic_t client_running;

// 클라이언트 서버 연결 
int 
client_connect(int argc, char *argv[]);

#endif