#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <limits.h>
#include <sys/socket.h>

int
main(int argc, char *argv[])
{
    int session_id;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    if (argc != 4)                                                               /* 인자 개수 확인 */
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 인자 개수 (expected: 4, got: %d)\n", argc);
        fprintf(stderr, "[Worker] 사용법: %s <session_id> <client_ip> <client_port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    LogContext log_ctx = {.fd = -1};                                             /* 로그 컨텍스트 초기화 (worker는 로그 파일 사용 안 함) */
    
    ServerState state = {0};
    state.running = 1;
    state.log_ctx = &log_ctx;
    setup_child_signal_handlers(&state);
    
    char *endptr;
    errno = 0;
    long sid_long = strtol(argv[1], &endptr, 10);                                /* session_id 파싱 */
    
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX)    /* 파싱 실패: 잘못된 형식, 오버플로우 */
    {
        fprintf(stderr, "[Worker] 에러: 잘못된 session_id 형식 '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    session_id = (int)sid_long;
    
    int client_sock = 3;                                                         /* exec로 전달받은 FD (dup2로 3번으로 고정) */
    
    /* FD 3이 유효한 소켓인지 확인 */
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1)   /* getsockopt 실패: 유효하지 않은 소켓 */
    {
        fprintf(stderr, "[Worker #%d] 에러: FD 3이 유효한 소켓이 아님\n", session_id);
        return EXIT_FAILURE;
    }
    
    /* 연결된 클라이언트 정보 조회 */
    if (getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len) == -1)  /* getpeername 실패: 연결 안 됨, 잘못된 소켓 */
    {
        fprintf(stderr, "[Worker #%d] 에러: getpeername() 실패: %s\n", session_id, strerror(errno));
        return EXIT_FAILURE;
    }
    
    printf("[Worker #%d (PID:%d)] exec() 성공!\n", session_id, getpid());
    
    child_process_main(client_sock, session_id, client_addr, &state);           /* 메인 처리 로직 실행 */
    
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    
    return EXIT_SUCCESS;
}
