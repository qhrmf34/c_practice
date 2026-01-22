#include "server_function.h"
#include <fcntl.h>

int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state)
{
    pid_t pid;
    char session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];
    if (state->worker_count >= MAX_WORKERS) 
    {
        log_message(state, LOG_WARNING, "fork_and_exec_worker() : 최대 Worker 수 도달 (%d개), 연결 거부", MAX_WORKERS);
        return -1;
    }
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL) 
    {
        log_message(state, LOG_ERROR, "fork_and_exec_worker() : inet_ntop() 실패: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid == -1) 
    {
        log_message(state, LOG_ERROR, "fork_and_exec_worker() : fork() 실패: %s", strerror(errno));
        return -1;
    } 
    else if (pid == 0) 
    { 
        close(serv_sock);
        if (dup2(clnt_sock, 3) == -1) 
        {
            fprintf(stderr, "fork_and_exec_worker() : [자식 #%d] dup2() 실패: %s\n", session_id, strerror(errno));
            _exit(1); 
        }
        if (clnt_sock != 3)
            close(clnt_sock);
        snprintf(session_str, sizeof(session_str), "%d", session_id);
        snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
        char *const argv[] = {(char*)"./worker", session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        fprintf(stderr, "fork_and_exec_worker() : [자식 #%d] execvp() 실패: %s\n", session_id, strerror(errno));
        if (errno == ENOENT || errno == EACCES)
            fprintf(stderr, "fork_and_exec_worker() : [자식 #%d] worker 실행파일 에러\n", session_id);
        _exit(127); //워커 파일을 못찾음
    }
    state->total_forks++;
    state->worker_count++;
    close(clnt_sock);
    log_message(state, LOG_INFO, "fork_and_exec_worker() : Worker 프로세스 생성 (PID: %d, Session #%d)", pid, session_id);
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
    int reaped = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        state->zombie_reaped++;
        state->worker_count--;
        reaped++;
    }
    if (reaped > 0)
        log_message(state, LOG_DEBUG, "handle_child_died() : 좀비 회수: %d개, 남은 Worker: %d개", state->zombie_reaped, state->worker_count);
}