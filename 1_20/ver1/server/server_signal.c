#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

// 서버용 시그널 핸들러 전역 포인터
static ServerState *g_server_state = NULL;
static WorkerState *g_worker_state = NULL;

// SIGINT/SIGTERM 핸들러 (서버)
static void 
server_shutdown_handler(int signo)
{
    int saved_errno = errno;
    if (g_server_state == NULL) 
    {
        errno = saved_errno;
        return;
    }
    if (signo == SIGINT)
        g_server_state->sigint_received = 1;
    else if (signo == SIGTERM)
        g_server_state->sigterm_received = 1;
    
    g_server_state->running = 0;
    errno = saved_errno;
}

// SIGCHLD 핸들러
static void 
server_sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    pid_t pid;
    int status;
    
    if (g_server_state == NULL) 
    {
        errno = saved_errno;
        return;
    }
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        g_server_state->zombie_reaped++;
        g_server_state->sigchld_received = 1;
        
        // Worker PID 배열에서 제거
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
    errno = saved_errno;
}

// SIGTERM 핸들러 (Worker)
static void 
worker_term_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    if (g_worker_state != NULL)
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
    
    struct sigaction sa_chld;
    sa_chld.sa_handler = server_sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 실패");
    
    struct sigaction sa_shutdown;
    sa_shutdown.sa_handler = server_shutdown_handler;
    sigemptyset(&sa_shutdown.sa_mask);
    sa_shutdown.sa_flags = 0;
    if (sigaction(SIGINT, &sa_shutdown, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGINT) 실패");
    if (sigaction(SIGTERM, &sa_shutdown, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패");
}

// Worker 시그널 설정
void setup_worker_signals(WorkerState *wstate)
{
    g_worker_state = wstate;
    
    struct sigaction sa_term;
    sa_term.sa_handler = worker_term_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    if (sigaction(SIGTERM, &sa_term, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패");
    
    signal(SIGINT, SIG_IGN); // Worker는 SIGINT 무시
}
