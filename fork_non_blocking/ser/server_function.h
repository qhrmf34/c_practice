#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>

#define BUF_SIZE 1024
#define PORT 9190

typedef struct {
    int sock;
    struct sockaddr_in addr;
    int session_id;
} ClientInfo;

// 클라이언트 처리 함수 (자식 프로세스에서 실행)
void
handle_client(int clnt_sock, int session_id, struct sockaddr_in clnt_addr, int parent_pipe);

// 서버 소켓 생성 및 설정
int
create_server_socket(void);

// 서버 실행 (메인 루프)
void
run_server(void);

// 시스템 리소스 출력 
void
print_resource_limits(void);

#endif