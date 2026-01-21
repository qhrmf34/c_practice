#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

/*
 * 서버 메인 루프
 * 
 * Self-Pipe 패턴:
 * 1. poll에 2개 FD 등록: serv_sock + signal_pipe[0]
 * 2. serv_sock readable → 새 연결 수락
 * 3. signal_pipe readable → 시그널 처리
 * 
 * Never Die 원칙:
 * - break 없음: 모든 에러는 continue
 * - 소켓 에러 → 재생성
 * - Worker 에러 → 해당 세션만 종료
 * - 서버는 절대 죽지 않음
 */
void
run_server(void)
{
    int serv_sock, clnt_sock, session_id = 0;
    struct sockaddr_in clnt_addr;
    
    // 서버 상태 초기화
    ServerState state = {0};
    state.running = 1;
    state.start_time = time(NULL);
    state.parent_pid = getpid();
    
    // 로그 컨텍스트 초기화
    LogContext log_ctx = {.fd = -1, .console_enabled = 1};
    log_init(&log_ctx);
    
    // 시그널 핸들러 설정 (Self-Pipe)
    setup_signal_handlers(&state);
    
    log_message(&log_ctx, LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(&log_ctx, LOG_INFO, "Port: %d", PORT);
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1) {
        log_message(&log_ctx, LOG_ERROR, "서버 소켓 생성 실패");
        return;
    }
    
    log_message(&log_ctx, LOG_INFO, "클라이언트 연결 대기 중 (Self-Pipe 패턴)");
    
    // poll FD 설정: [0]=서버소켓, [1]=시그널파이프
    struct pollfd pfds[2];
    pfds[0].fd = serv_sock;
    pfds[0].events = POLLIN;
    pfds[1].fd = state.signal_pipe[0];
    pfds[1].events = POLLIN;
    
    // 메인 루프: running=0이 될 때까지 실행
    while (state.running) {
        pfds[0].revents = 0;
        pfds[1].revents = 0;
        
        // poll: 1초 타임아웃 (주기적으로 상태 체크)
        int ret = poll(pfds, 2, 1000);
        
        if (ret == -1) {
            if (errno == EINTR)  // 시그널 인터럽트 → 정상, 재시도
                continue;
            log_message(&log_ctx, LOG_ERROR, "poll() 실패: %s", strerror(errno));
            continue;  //  break 없음
        }
        
        if (ret == 0)  // 타임아웃 → 정상
            continue;
        
        // [1] 시그널 파이프 체크 (우선 순위 높음)
        if (pfds[1].revents & POLLIN) {
            unsigned char signo;
            // pipe에서 시그널 번호 읽기 (여러 개 올 수 있음)
            while (read(state.signal_pipe[0], &signo, 1) > 0)
                handle_signal(&state, signo, &log_ctx);
        }
        
        // [0] 서버 소켓 에러 체크
        if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_message(&log_ctx, LOG_ERROR, "서버 소켓 에러 감지, 재생성 시도");
            close(serv_sock);
            serv_sock = create_server_socket();
            if (serv_sock == -1) {
                log_message(&log_ctx, LOG_ERROR, "서버 소켓 재생성 실패, 1초 후 재시도");
                sleep(1);
                continue;  //  break 없음, 계속 시도
            }
            pfds[0].fd = serv_sock;
            log_message(&log_ctx, LOG_INFO, "서버 소켓 재생성 성공");
            continue;
        }
        
        // [0] 서버 소켓 readable → 새 연결
        if (pfds[0].revents & POLLIN) {
            clnt_sock = accept_client(serv_sock, &clnt_addr);
            if (clnt_sock == -1)
                continue;  // accept 실패 → 다음 연결 대기
            
            // 종료 중이면 새 연결 거부
            if (!state.running) {
                log_message(&log_ctx, LOG_INFO, "종료 중이므로 새 연결 거부");
                close(clnt_sock);
                continue;
            }
            
            session_id++;
            
            // 클라이언트 IP 출력
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clnt_addr.sin_addr, client_ip, sizeof(client_ip));
            log_message(&log_ctx, LOG_INFO, "새 연결: %s:%d (Session #%d)", 
                       client_ip, ntohs(clnt_addr.sin_port), session_id);
            
            // Worker 프로세스 생성 (fork-exec)
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state, &log_ctx) == -1) {
                close(clnt_sock);
                log_message(&log_ctx, LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
    }
    
    // 종료 처리
    log_message(&log_ctx, LOG_INFO, "서버 종료 시작");
    shutdown_all_workers(&state, &log_ctx);
    
    // 리소스 정리
    close(serv_sock);
    close(state.signal_pipe[0]);
    close(state.signal_pipe[1]);
    
    print_resource_limits();
    log_message(&log_ctx, LOG_INFO, "=== 서버 정상 종료 완료 ===");
    
    log_cleanup(&log_ctx);
}
