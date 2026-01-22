#define _POSIX_C_SOURCE 200809L
#include "mstHead.h"
#include "server_function.h"

void
run_server(void)
{
    int serv_sock, clnt_sock, session_id = 0;
    struct sockaddr_in clnt_addr;
    
    LogContext log_ctx = {.fd = -1};                                             /* 로그 컨텍스트 초기화 */
    
    ServerState state = {};
    state.running = 1;
    state.start_time = time(NULL);
    state.parent_pid = getpid();                                                 /* 부모 PID 저장 (PGID로 사용) */
    state.log_ctx = &log_ctx;

    if (setpgid(0, 0) == -1)                                                     /* 부모를 프로세스 그룹 리더로 설정 */
        fprintf(stderr, "[경고] setpgid(0,0) 실패: %s\n", strerror(errno));

    state.pgid = getpgrp();                                                      /* 현재 프로세스 그룹 ID 조회 */
    
    /* killpg 사용 가능 여부 판단 (PGID == parent_pid일 때만) */
    if (state.pgid == state.parent_pid) 
        state.use_killpg = 1;                                                    /* 그룹 격리 성공: killpg 사용 가능 */
    else 
    {
        state.use_killpg = 0;                                                    /* 그룹 격리 실패: fallback 필요 */
        fprintf(stderr, "[경고] 프로세스 그룹 격리 실패 (PGID=%d != PID=%d)\n", )state.pgid, (int)state.parent_pid);
    }
    
    setup_signal_handlers(&state);
    log_init(&log_ctx);
    // sleep(1);
    // test_crash_with_stack();
    log_message(&log_ctx, LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(&log_ctx, LOG_INFO, "Port: %d", PORT);
    log_message(&log_ctx, LOG_INFO, "프로세스 그룹 ID(PGID): %d", (int)state.pgid);
    
    serv_sock = create_server_socket();
    if (serv_sock == -1)                                                         /* 소켓 생성 실패: 권한, 리소스 부족 */
    {
        log_message(&log_ctx, LOG_ERROR, "서버 소켓 생성 실패");
        log_close(&log_ctx);
        return;
    }
    
    log_message(&log_ctx, LOG_INFO, "클라이언트 연결 대기 중");
    
    struct pollfd pfd = {.fd = serv_sock, .events = POLLIN, .revents = 0};
    
    while (state.running) 
    {
        handle_child_died(&state);                                               /* 종료된 자식 프로세스 회수 */
        
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 1000);                                           /* 1초 타임아웃으로 주기적 체크 */
        if (ret == -1)                                                           /* poll 실패 */
        {
            if (errno == EINTR)                                                  /* 시그널로 인한 중단은 정상 */
                continue;
            log_message(&log_ctx, LOG_ERROR, "poll() 실패: %s", strerror(errno));  /* 기타 에러: 잘못된 fd, 메모리 부족 */
            continue;
        }
        else if (ret == 0)                                                       /* 타임아웃 - 다음 루프로 */
            continue;
        
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL |POLLNVAL))                        /* 소켓 에러 이벤트 */
        {
            log_message(&log_ctx, LOG_ERROR, "run_server() poll error[%d%s]", errno, strerror(errno));
            continue;
        }
        // {
        //     if (pfd.revents & (POLLERR | POLLHUP | )
        //     if (pfd.revents & POLLERR)
        //         log_message(&log_ctx, LOG_ERROR, "poll: POLLERR (소켓 내부 오류)");
        //     if (pfd.revents & POLLHUP)
        //         log_message(&log_ctx, LOG_WARNING, "poll: POLLHUP (상대가 끊음/half-close 가능)");
        //     if (pfd.revents & POLLNVAL)
        //         log_message(&log_ctx, LOG_ERROR, "poll: POLLNVAL (fd가 유효하지 않음/닫혔을 가능성)");     
        //     log_message(&log_ctx, LOG_ERROR, "서버 소켓 에러");                  /* POLLERR: 소켓 오류, POLLHUP: 연결 끊김, POLLNVAL: 잘못된 fd */
        //     continue;
        // }
        else if (pfd.revents & POLLIN)                                           /* 새 연결 대기 중 */
        {
            clnt_sock = accept_client(serv_sock, &clnt_addr, &state);
            if (clnt_sock == -1)                                                 /* accept 실패 또는 종료 신호 */
                continue;
            
            // if (!state.running)                                                  /* 종료 중이면 새 연결 거부 */
            // {
            //     log_message(&log_ctx, LOG_INFO, "종료 중이므로 새 연결 거부");
            //     close(clnt_sock);
            //     continue;
            // }
            
            session_id++;
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clnt_addr.sin_addr, client_ip, sizeof(client_ip));
            log_message(&log_ctx, LOG_INFO, "새 연결 수락: %s:%d (Session #%d)", 
                       client_ip, ntohs(clnt_addr.sin_port), session_id);
            
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state) == -1) 
            {
                close(clnt_sock);                                                /* fork 실패 시 소켓 정리 */
                log_message(&log_ctx, LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
        else
        {
            log_message(&log_ctx, LOG_WARNING, "ret>0이지만 처리한 이벤트가 아님 %x", pfd.revents);
            continue;
        }
    }
    
    shutdown_workers(&state);                                                    /* 모든 워커 정리 */
    
    if (close(serv_sock) == -1)                                                  /* 서버 소켓 닫기 */
        log_message(&log_ctx, LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    else
        log_message(&log_ctx, LOG_INFO, "서버 소켓 닫기 완료");
    
    final_cleanup(&state);
    log_close(&log_ctx);                                                         /* 로그 파일 닫기 */
}
