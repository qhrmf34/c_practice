#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // getpid() 선언
#include <signal.h>

// worker 프로그램의 main 함수
// Usage: worker <client_sock_fd> <session_id>
int
main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <client_sock_fd> <session_id>\n", argv[0]);
        exit(1);
    }
    
    // 인자 파싱
    int client_sock = atoi(argv[1]);
    int session_id = atoi(argv[2]);
    
    // 유효성 검사
    if (client_sock < 0)
    {
        fprintf(stderr, "[Worker] Invalid client socket FD: %d\n", client_sock);
        exit(1);
    }
    
    printf("[Worker] Started with FD=%d, Session=%d, PID=%d\n", 
           client_sock, session_id, getpid());
    
    // Crash handler 설정 (자식용)
    setup_child_signal_handlers();
    
    // SIGINT, SIGTERM 무시 (부모가 처리)
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    
    // Worker 메인 함수 실행
    worker_process_main(client_sock, session_id);
    
    // 정상 종료
    return 0;
}