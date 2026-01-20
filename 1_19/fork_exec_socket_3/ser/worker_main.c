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
#include <signal.h>

/*
 * Worker 프로그램 - exec()로 실행되어 클라이언트를 처리
 * 
 * 실행 방식: ./worker <client_sock_fd> <session_id> <client_ip> <client_port>
 * 
 * 에러 처리:
 * - 인자 개수 검증
 * - FD 유효성 검증
 * - IP/Port 파싱 검증
 * - 모든 실패는 stderr 로그 + exit code 반환
 */

int
main(int argc, char *argv[])
{
    int client_sock;
    int session_id;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    struct sockaddr_in client_addr;
    
    //  인자 개수 검증 
    if (argc != 5)
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 인자 개수 (expected: 5, got: %d)\n", argc);
        fprintf(stderr, "[Worker] 사용법: %s <client_sock_fd> <session_id> <client_ip> <client_port>\n", 
                argv[0]);
        return EXIT_FAILURE;
    }
    
    // === SIGPIPE 무시 (중요!) ===
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
    {
        fprintf(stderr, "[Worker] sigaction(SIGPIPE) 설정 실패: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    
    // === SIGINT만 무시 (터미널 Ctrl+C는 부모가 처리) ===
    struct sigaction sa_ign;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa_ign, NULL) == -1)
    {
        fprintf(stderr, "[Worker] sigaction(SIGINT) 설정 실패: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    
    // === SIGTERM은 정상 처리 (부모의 shutdown 신호) ===
    // SIGTERM을 받으면 child_process_main에서 graceful shutdown
    // (child_process_main에서 전역 플래그 확인)
    
    // Worker용 signal handlers 설정 (crash handler + SIGTERM)
    setup_child_signal_handlers();
    
    
    // === client_sock FD 파싱 ===
    char *endptr;
    errno = 0;
    long fd_long = strtol(argv[1], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || fd_long < 0 || fd_long > INT_MAX)
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 FD 형식 '%s' (errno: %d)\n", 
                argv[1], errno);
        return EXIT_FAILURE;
    }
    client_sock = (int)fd_long;
    
    // === session_id 파싱 ===
    errno = 0;
    long sid_long = strtol(argv[2], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX)
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 session_id 형식 '%s' (errno: %d)\n", 
                argv[2], errno);
        return EXIT_FAILURE;
    }
    session_id = (int)sid_long;
    
    // === client_ip 파싱 및 검증 ===
    if (strlen(argv[3]) >= INET_ADDRSTRLEN)
    {
        fprintf(stderr, "[Worker #%d] 에러: IP 주소가 너무 길음 (len: %zu)\n", 
                session_id, strlen(argv[3]));
        close(client_sock);
        return EXIT_FAILURE;
    }
    strncpy(client_ip, argv[3], INET_ADDRSTRLEN - 1);
    client_ip[INET_ADDRSTRLEN - 1] = '\0';
    
    // IP 형식 검증
    if (inet_pton(AF_INET, client_ip, &client_addr.sin_addr) != 1)
    {
        fprintf(stderr, "[Worker #%d] 에러: 잘못된 IP 주소 형식 '%s'\n", 
                session_id, client_ip);
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    // === client_port 파싱 ===
    errno = 0;
    long port_long = strtol(argv[4], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || port_long < 0 || port_long > 65535)
    {
        fprintf(stderr, "[Worker #%d] 에러: 잘못된 포트 번호 '%s' (errno: %d)\n", 
                session_id, argv[4], errno);
        close(client_sock);
        return EXIT_FAILURE;
    }
    client_port = (int)port_long;
    
    // === FD 유효성 검증 ===
    // fcntl로 FD가 유효한지 확인
    int flags = fcntl(client_sock, F_GETFL);
    if (flags == -1)
    {
        fprintf(stderr, "[Worker #%d] 에러: FD %d가 유효하지 않음 (fcntl failed: %s)\n", 
                session_id, client_sock, strerror(errno));
        return EXIT_FAILURE;
    }
    
    // FD가 소켓인지 확인 (getsockopt로 검증)
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1)
    {
        fprintf(stderr, "[Worker #%d] 에러: FD %d가 소켓이 아님 (getsockopt failed: %s)\n", 
                session_id, client_sock, strerror(errno));
        close(client_sock);
        return EXIT_FAILURE;
    }
    
    // === sockaddr_in 구조체 완성 ===
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    
    // === 성공 로그 및 처리 시작 ===
    printf("[Worker #%d (PID:%d)] exec() 성공!\n", session_id, getpid());
    printf("[Worker #%d] 인자: sock_fd=%d, session_id=%d, ip=%s, port=%d\n",
           session_id, client_sock, session_id, client_ip, client_port);
    
    // === 클라이언트 처리 (기존 child_process_main 호출) ===
    child_process_main(client_sock, session_id, client_addr);
    
    // === 정상 종료 ===
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    
    return EXIT_SUCCESS;
}