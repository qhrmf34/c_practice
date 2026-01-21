#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state)
{
    pid_t pid;
    char session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];

    if (state->worker_count >= MAX_WORKERS) {
        log_message(LOG_WARNING, "최대 Worker 수 도달 (%d개), 연결 거부", MAX_WORKERS);
        return -1;
    }

    snprintf(session_str, sizeof(session_str), "%d", session_id);
    inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str));
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    pid = fork();
    
    if (pid == -1) {
        log_message(LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        setup_child_signal_handlers();
        close(serv_sock);
        close(state->signal_pipe[0]);
        close(state->signal_pipe[1]);
        
        if (dup2(clnt_sock, 3) == -1) {
            fprintf(stderr, "[자식 #%d] dup2() 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        if (clnt_sock != 3)
            close(clnt_sock);
        
        char *const argv[] = {"./worker", session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        _exit(127);
    }
    
    state->total_forks++;
    state->worker_pids[state->worker_count++] = pid;
    
    close(clnt_sock);
    log_message(LOG_INFO, "Worker 생성 (PID: %d, Session #%d)", pid, session_id);
    
    return 0;
}
