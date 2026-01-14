#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H

#define BUF_SIZE 1024
#define IO_COUNT 10  // 각 클라이언트가 수행할 IO 횟수

// 클라이언트 실행 함수
void
client_run(const char *ip, int port, int client_id);

#endif