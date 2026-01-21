#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

// Worker 생성 함수
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id,
                         struct sockaddr_in *clnt_addr, ServerState *state)
{
    char fd_str[32], session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];
    pid_t pid;

    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL) 
    {
        log_message(LOG_ERROR, "inet_ntop() 실패");
        return -1;
    }
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));

    pid = fork();
    
    if (pid == -1) 
    {
        state->fork_errors++;
        log_message(LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) 
    { // 자식 프로세스
        WorkerState wstate = {0};
        setup_worker_signals(&wstate);
        
        close(serv_sock);
        
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags != -1) 
        {
            flags &= ~FD_CLOEXEC;
            fcntl(clnt_sock, F_SETFD, flags);
        }
        
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        
        char *const argv[] = {"./worker", fd_str, session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        _exit(127);
    }
    
    // 부모 프로세스
    state->total_forks++;
    if (state->worker_count < MAX_WORKERS)
        state->worker_pids[state->worker_count++] = pid;
    
    close(clnt_sock);
    log_message(LOG_INFO, "Worker 생성: PID=%d, Session=#%d", pid, session_id);
    return 0;
}