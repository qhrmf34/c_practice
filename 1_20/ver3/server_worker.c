#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state)
{
    pid_t pid;
    char fd_str[32], session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];

    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL) {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s", strerror(errno));
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    pid = fork();
    
    if (pid == -1) {
        if (errno == EAGAIN)
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계");
        else
            log_message(LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        setup_child_signal_handlers(state);
        close(serv_sock);
        
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1) {
            fprintf(stderr, "[자식 #%d] fcntl(F_GETFD) 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        flags &= ~FD_CLOEXEC;
        if (fcntl(clnt_sock, F_SETFD, flags) == -1) {
            fprintf(stderr, "[자식 #%d] fcntl(F_SETFD) 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        
        char *const argv[] = {"./worker", fd_str, session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        if (errno == ENOENT)
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음\n", session_id);
        else if (errno == EACCES)
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음\n", session_id);
        
        _exit(127);
    }
    
    state->total_forks++;
    
    if (state->worker_count < MAX_WORKERS)
        state->worker_pids[state->worker_count++] = pid;
    else
        log_message(LOG_WARNING, "Worker PID 배열 가득참");
    
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
    log_message(LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", state->zombie_reaped, state->worker_count);
}
