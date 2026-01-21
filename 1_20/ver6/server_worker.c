#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

/*
 * Worker 프로세스 생성 (fork-exec)
 * 
 * @param serv_sock: 서버 소켓 (자식은 닫음)
 * @param clnt_sock: 클라이언트 소켓 (FD=3으로 복사)
 * @param session_id: 세션 ID
 * @param clnt_addr: 클라이언트 주소
 * @param state: 서버 상태
 * @param log_ctx: 로그 컨텍스트
 * 
 * @return 0=성공, -1=실패
 * 
 * 동작:
 * 1. MAX_WORKERS 체크 (초과하면 거부)
 * 2. fork() 실행
 * 3. 자식: dup2(clnt_sock, 3) → FD 고정
 * 4. 자식: exec("./worker") 실행
 * 5. 부모: PID 배열에 추가, clnt_sock 닫기
 * 
 * FD=3 고정:
 * - Worker는 항상 FD 3에서 클라이언트 소켓 사용
 * - argv로 FD 전달 불필요 (코드 간소화)
 * - 디버깅 쉬움 (항상 3번)
 */
int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, 
                    struct sockaddr_in *clnt_addr, ServerState *state, LogContext *log_ctx)
{
    pid_t pid;
    char session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];

    // [1] MAX_WORKERS 체크 (리소스 제한)
    if (state->worker_count >= MAX_WORKERS) {
        log_message(log_ctx, LOG_WARNING, "최대 Worker 수 도달 (%d개), 연결 거부", MAX_WORKERS);
        return -1;
    }

    // exec 인자 준비
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str));
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    // [2] fork() 실행
    pid = fork();
    
    if (pid == -1) {
        log_message(log_ctx, LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    // [3] 자식 프로세스
    if (pid == 0) {
        // 시그널 핸들러 설정 (자식용)
        WorkerContext ctx = {.running = 1, .session_id = session_id, .worker_pid = getpid()};
        setup_worker_signal_handlers(&ctx);
        
        // 불필요한 FD 닫기
        close(serv_sock);                // 서버 소켓 (자식 불필요)
        close(state->signal_pipe[0]);   // Self-pipe (자식 불필요)
        close(state->signal_pipe[1]);
        
        // [핵심] FD=3으로 고정
        if (dup2(clnt_sock, 3) == -1) {
            fprintf(stderr, "[자식 #%d] dup2() 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        if (clnt_sock != 3)
            close(clnt_sock);  // 원본 FD 닫기 (3번만 유지)
        
        // exec() 실행
        char *const argv[] = {"./worker", session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        // exec 실패 (여기 도달하면 에러)
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        _exit(127);  // exec 실패 코드
    }
    
    // [4] 부모 프로세스
    state->total_forks++;
    state->worker_pids[state->worker_count++] = pid;  // PID 배열에 추가
    
    close(clnt_sock);  // 부모는 clnt_sock 불필요
    log_message(log_ctx, LOG_INFO, "Worker 생성 (PID: %d, Session #%d)", pid, session_id);
    
    return 0;
}
