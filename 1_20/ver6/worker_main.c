#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int session_id;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    if (argc != 4) {
        fprintf(stderr, "[Worker] 잘못된 인자 (expected: 4, got: %d)\n", argc);
        return EXIT_FAILURE;
    }
    
    session_id = atoi(argv[1]);
    
    int client_sock = 3;  // FD 고정
    
    // FD 검증
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1) {
        fprintf(stderr, "[Worker #%d] FD 3이 유효한 소켓이 아님\n", session_id);
        return EXIT_FAILURE;
    }
    
    if (getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len) == -1) {
        fprintf(stderr, "[Worker #%d] getpeername() 실패\n", session_id);
        return EXIT_FAILURE;
    }
    
    printf("[Worker #%d (PID:%d)] exec() 성공\n", session_id, getpid());
    
    WorkerContext ctx = {.running = 1, .session_id = session_id, .worker_pid = getpid()};
    LogContext log_ctx = {.fd = -1, .console_enabled = 1};
    
    child_process_main(client_sock, session_id, client_addr, &ctx, &log_ctx);
    
    return EXIT_SUCCESS;
}
