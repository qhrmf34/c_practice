#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

static ServerState *g_state = NULL;  // 시그널 핸들러용 전역 포인터

static void 
signal_handler(int signo) 
{
    int saved_errno = errno;
    
    if (signo == SIGCHLD) 
    {  // 자식 프로세스 종료
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
        {
            if (g_state == NULL) continue;
            g_state->zombie_reaped++;
            g_state->child_died = 1;
            for (int i = 0; i < g_state->worker_count; i++) 
            {
                if (g_state->worker_pids[i] == pid) 
                {
                    g_state->worker_pids[i] = g_state->worker_pids[g_state->worker_count - 1];
                    g_state->worker_count--;
                    break;
                }
            }
        }
    } 
    else if (signo == SIGINT || signo == SIGTERM) 
    {  // 서버 종료
        if (g_state && getpid() == g_state->parent_pid) 
            g_state->running = 0;  // 부모만
        else if (g_state) 
            g_state->running = 0;  // 워커
    }
    
    errno = saved_errno;
}

void 
setup_signal_handlers(ServerState *state) 
{
    g_state = state;
    
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGPIPE) 실패: %s", strerror(errno));
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 실패: %s", strerror(errno));
    
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGINT) 실패: %s", strerror(errno));
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패: %s", strerror(errno));
}

void 
setup_child_signal_handlers(ServerState *state) 
{
    g_state = state;
    signal(SIGINT, SIG_IGN);
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(LOG_ERROR, "sigaction(SIGTERM) 실패: %s", strerror(errno));
}