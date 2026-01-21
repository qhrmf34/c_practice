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
    if (argc != 5) 
    {
        fprintf(stderr, "[Worker] 인자 개수 오류: %d (expected: 5)\n", argc);
        fprintf(stderr, "[Worker] 사용법: %s <fd> <session_id> <ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    WorkerState wstate = {0};
    setup_worker_signals(&wstate);

    // FD 파싱
    char *endptr;
    errno = 0;
    long fd_long = strtol(argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || fd_long < 0 || fd_long > INT_MAX) 
    {
        fprintf(stderr, "[Worker] FD 파싱 오류: '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    int client_sock = (int)fd_long;
    
    // Session ID 파싱
    errno = 0;
    long sid_long = strtol(argv[2], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX) 
    {
        fprintf(stderr, "[Worker] Session ID 파싱 오류: '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }
    int session_id = (int)sid_long;
    
    // IP 파싱
    char client_ip[INET_ADDRSTRLEN];
    if (strlen(argv[3]) >= INET_ADDRSTRLEN) 
    {
        fprintf(stderr, "[Worker #%d] IP 주소 너무 김\n", session_id);
        close(client_sock);
        return EXIT_FAILURE;
    }
    strncpy(client_ip, argv[3], INET_ADDRSTRLEN - 1);
    client_ip[INET_ADDRSTRLEN - 1] = '\0';
    
    struct sockaddr_in client_addr;
    if (inet_pton(AF_INET, client_ip, &client_addr.sin_addr) != 1) 
    {
        fprintf(stderr, "[Worker #%d] IP 형식 오류: '%s'\n", session_id, client_ip);
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    // Port 파싱
    errno = 0;
    long port_long = strtol(argv[4], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || port_long < 0 || port_long > 65535) 
    {
        fprintf(stderr, "[Worker #%d] Port 파싱 오류: '%s'\n", session_id, argv[4]);
        close(client_sock);
        return EXIT_FAILURE;
    }
    int client_port = (int)port_long;
    
    // FD 유효성 검증
    int flags = fcntl(client_sock, F_GETFL);
    if (flags == -1) 
    {
        fprintf(stderr, "[Worker #%d] FD 유효성 검증 실패\n", session_id);
        return EXIT_FAILURE;
    }
    
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1) 
    {
        fprintf(stderr, "[Worker #%d] 소켓 검증 실패\n", session_id);
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    // sockaddr_in 완성
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    
    printf("[Worker #%d (PID:%d)] exec 성공\n", session_id, getpid());
    printf("[Worker #%d] 인자: fd=%d, session=%d, ip=%s, port=%d\n",
           session_id, client_sock, session_id, client_ip, client_port);
    
    // 클라이언트 처리
    child_process_main(client_sock, session_id, client_addr, &wstate);
    
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    return EXIT_SUCCESS;
}
