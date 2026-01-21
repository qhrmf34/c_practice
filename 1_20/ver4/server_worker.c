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

    if (state->worker_count >= MAX_WORKERS) 
    {
        log_message(LOG_WARNING, "최대 Worker 수 도달 (%d개), 연결 거부", MAX_WORKERS);
        return -1;
    }

    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL) 
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s", strerror(errno));
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    pid = fork();
    
    if (pid == -1) 
    {
        if (errno == EAGAIN)
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계");
        else
            log_message(LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) 
    {
        setup_child_signal_handlers(state);
        close(serv_sock);
        
        if (dup2(clnt_sock, 3) == -1) 
        {
            fprintf(stderr, "[자식 #%d] dup2() 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        if (clnt_sock != 3)
            close(clnt_sock);
        
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        
        char *const argv[] = {"./worker", session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        if (errno == ENOENT)
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음\n", session_id);
        else if (errno == EACCES)
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음\n", session_id);
        
        _exit(127);
    }
    
    state->total_forks++;
    state->worker_pids[state->worker_count++] = pid;
    
    close(clnt_sock);
    log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", pid, session_id);
    
    return 0;
}

void
handle_child_died(ServerState *state)
{
    if (!state->child_died)
        return;
    
    state->child_died = 0;
    
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        state->zombie_reaped++;
        
        for (int i = 0; i < state->worker_count; i++) 
        {
            if (state->worker_pids[i] == pid) 
            {
                state->worker_pids[i] = state->worker_pids[state->worker_count - 1];
                state->worker_count--;
                break;
            }
        }
    }
    
    log_message(LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", state->zombie_reaped, state->worker_count);
}
