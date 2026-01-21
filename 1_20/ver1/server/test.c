#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

static ServerState *g_server_state = NULL;
static WorkerState *g_worker_state = NULL;

// 통합 시그널 핸들러 (서버 + 워커 모두 처리)
static void 
unified_signal_handler(int signo)
{
    int saved_errno = errno;
    
    // 서버 시그널 처리
    if (g_server_state != NULL) 
    {
        if (signo == SIGINT) 
        {
            g_server_state->sigint_received = 1;
            g_server_state->running = 0;
        } 
        else if (signo == SIGTERM) 
        {
            g_server_state->sigterm_received = 1;
            g_server_state->running = 0;
            
        } 
        else if (signo == SIGCHLD) 
        {
            pid_t pid;
            int status;
            
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
            {
                g_server_state->zombie_reaped++;
                g_server_state->sigchld_received = 1;
                
                for (int i = 0; i < g_server_state->worker_count; i++) 
                {
                    if (g_server_state->worker_pids[i] == pid) 
                    {
                        g_server_state->worker_pids[i] = g_server_state->worker_pids[g_server_state->worker_count - 1];
                        g_server_state->worker_count--;
                        break;
                    }
                }
            }
        }
    }
    // 워커 시그널 처리
    if (g_worker_state != NULL) 
        if (signo == SIGTERM)
            g_worker_state->shutdown_requested = 1;
    
    errno = saved_errno;
}

// 서버 시그널 설정
void 
setup_server_signals(ServerState *state)
{
    g_server_state = state;
    
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGPIPE) 실패");
    
    struct sigaction sa;
    sa.sa_handler = unified_signal_handler; // 통합 핸들러 사용
    sigemptyset(&sa.sa_mask);
    
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;    // SIGCHLD
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 실패");
    
    sa.sa_flags = 0;    // SIGINT/SIGTERM
    if (sigaction(SIGINT, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGINT) 실패");
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패");
}
void 
setup_worker_signals(WorkerState *wstate)// 워커 시그널 설정
{
    g_worker_state = wstate;
    
    struct sigaction sa;
    sa.sa_handler = unified_signal_handler; // 통합 핸들러 사용
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패");
    
    signal(SIGINT, SIG_IGN);
}