#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static int g_signal_pipe_fd = -1;

static void
signal_handler(int signo)
{
    int saved_errno = errno;
    unsigned char sig = (unsigned char)signo;
    
    if (g_signal_pipe_fd >= 0)
        write(g_signal_pipe_fd, &sig, 1);
    
    errno = saved_errno;
}

void
setup_signal_handlers(ServerState *state)
{
    if (pipe(state->signal_pipe) == -1) {
        log_message(LOG_ERROR, "pipe() 실패: %s", strerror(errno));
        return;
    }
    
    fcntl(state->signal_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(state->signal_pipe[1], F_SETFL, O_NONBLOCK);
    
    g_signal_pipe_fd = state->signal_pipe[1];
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    
    log_message(LOG_INFO, "시그널 핸들러 설정 완료 (self-pipe)");
}

void
setup_child_signal_handlers(void)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);
}
