#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state)
{
    pid_t pid;
    char session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];

    if (state->worker_count >= MAX_WORKERS)                                      /* Worker 수 한계 도달 */
    {
        log_message(state->log_ctx, LOG_WARNING, "최대 Worker 수 도달 (%d개), 연결 거부", MAX_WORKERS);
        return -1;
    }

    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL)  /* IP 변환 실패: 드물지만 가능 */
    {
        log_message(state->log_ctx, LOG_ERROR, "inet_ntop() 실패: %s", strerror(errno));
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    pid = fork();
    
    if (pid == -1)                                                               /* fork 실패 */
    {
        if (errno == EAGAIN)                                                     /* 프로세스 한계 도달, 메모리 부족 */
            log_message(state->log_ctx, LOG_ERROR, "fork() 실패: 프로세스 리소스 한계");
        else
            log_message(state->log_ctx, LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    else if (pid == 0)                                                           /* 자식 프로세스 */
    {
        /* 자식을 부모의 프로세스 그룹에 편입 */
        if (setpgid(0, state->pgid) == -1)                                 /* 실패 가능: 부모가 다른 세션, race condition */
        {
            /* 실패해도 치명적이지 않음, 로그만 남기고 계속 진행 */
        }
        
        setup_child_signal_handlers(state);
        close(serv_sock);                                                        /* 자식은 서버 소켓 불필요 */
        
        if (dup2(clnt_sock, 3) == -1)                                            /* FD 3으로 복제 (exec 후 사용) */
        {
            fprintf(stderr, "[자식 #%d] dup2() 실패: %s\n", session_id, strerror(errno));  /* dup2 실패: fd 한계, 메모리 부족 */
            _exit(EXIT_FAILURE);
        }
        
        if (clnt_sock != 3)                                                      /* 원본 소켓은 닫기 */
            close(clnt_sock);

        char *const argv[] = {"./worker", session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);                                                /* worker 프로그램 실행 */
        
        /* exec 실패 시에만 여기 도달 */
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        if (errno == ENOENT)                                                     /* 실행 파일 없음 */
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음\n", session_id);
        else if (errno == EACCES)                                                /* 실행 권한 없음 */
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음\n", session_id);
        
        _exit(127);                                                              /* exec 실패 시 127 종료 코드 */
    }
    
    /* 부모 프로세스: race condition 대비 자식을 그룹에 편입 시도 */
    if (setpgid(pid, state->pgid) == -1)                                   /* 실패 가능: 자식이 이미 exec, EACCES */
    {
        if (errno != EACCES)                                                     /* EACCES는 흔한 race, 무시 */
            log_message(state->log_ctx, LOG_DEBUG, "setpgid(%d) 실패: %s", pid, strerror(errno));
    }
    
    state->total_forks++;
    state->worker_pids[state->worker_count++] = pid;
    
    close(clnt_sock);                                                            /* 부모는 클라이언트 소켓 닫기 (자식이 처리) */
    log_message(state->log_ctx, LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", pid, session_id);
    
    return 0;
}

void
handle_child_died(ServerState *state)
{
    if (!state->child_died)                                                      /* SIGCHLD 플래그 확인 */
        return;
    
    state->child_died = 0;                                                       /* 플래그 초기화 */
    
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)                            /* 모든 종료된 자식 회수 */
    {
        state->zombie_reaped++;
        
        /* worker_pids 배열에서 제거 */
        for (int i = 0; i < state->worker_count; i++) 
        {
            if (state->worker_pids[i] == pid) 
            {
                state->worker_pids[i] = state->worker_pids[state->worker_count - 1];  /* 마지막 요소와 교체 */
                state->worker_count--;
                break;
            }
        }
    }
    
    log_message(state->log_ctx, LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", 
               state->zombie_reaped, state->worker_count);
}
