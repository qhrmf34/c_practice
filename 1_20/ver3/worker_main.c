#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <limits.h>

int
main(int argc, char *argv[])
{
    int client_sock, session_id, client_port;
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in client_addr;
    
    if (argc != 5) 
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 인자 개수 (expected: 5, got: %d)\n", argc);
        fprintf(stderr, "[Worker] 사용법: %s <client_sock_fd> <session_id> <client_ip> <client_port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    ServerState state = {0};
    state.running = 1;
    setup_child_signal_handlers(&state);
    
    char *endptr;
    errno = 0;
    long fd_long = strtol(argv[1], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || fd_long < 0 || fd_long > INT_MAX) 
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 FD 형식 '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    client_sock = (int)fd_long;
    
    errno = 0;
    long sid_long = strtol(argv[2], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX) {
        fprintf(stderr, "[Worker] 에러: 잘못된 session_id 형식 '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }
    session_id = (int)sid_long;
    
    if (strlen(argv[3]) >= INET_ADDRSTRLEN) {
        fprintf(stderr, "[Worker #%d] 에러: IP 주소가 너무 길음\n", session_id);
        close(client_sock);
        return EXIT_FAILURE;
    }
    strncpy(client_ip, argv[3], INET_ADDRSTRLEN - 1);
    client_ip[INET_ADDRSTRLEN - 1] = '\0';
    
    if (inet_pton(AF_INET, client_ip, &client_addr.sin_addr) != 1) {
        fprintf(stderr, "[Worker #%d] 에러: 잘못된 IP 주소 형식 '%s'\n", session_id, client_ip);
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    errno = 0;
    long port_long = strtol(argv[4], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || port_long < 0 || port_long > 65535) {
        fprintf(stderr, "[Worker #%d] 에러: 잘못된 포트 번호 '%s'\n", session_id, argv[4]);
        close(client_sock);
        return EXIT_FAILURE;
    }
    client_port = (int)port_long;
    
    int flags = fcntl(client_sock, F_GETFL);
    if (flags == -1) {
        fprintf(stderr, "[Worker #%d] 에러: FD %d가 유효하지 않음\n", session_id, client_sock);
        return EXIT_FAILURE;
    }
    
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1) {
        fprintf(stderr, "[Worker #%d] 에러: FD %d가 소켓이 아님\n", session_id, client_sock);
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    
    printf("[Worker #%d (PID:%d)] exec() 성공!\n", session_id, getpid());
    
    child_process_main(client_sock, session_id, client_addr, &state);
    
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    
    return EXIT_SUCCESS;
}
