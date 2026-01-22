#include "server_function.h"
#include <execinfo.h>

static ServerState *g_state = NULL;
static void 
signal_handler(int signo)
{
    int saved_errno = errno;
    if (signo == SIGCHLD) 
    {
        if (g_state)
            g_state->child_died = 1;
    } 
    else if (signo == SIGINT || signo == SIGTERM) 
    {
        if (g_state)
            g_state->running = 0;
    }
    errno = saved_errno;
}
static void 
crash_handler(int sig)
{
    void *buffer[MAX_FRAMES];
    int nptrs;
    if (g_state && getpid() == g_state->parent_pid)
    {
        const char msg[] = "\n!!! PARENT CRASH !!!\n=== Stack Trace ===\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        nptrs = backtrace(buffer, MAX_FRAMES);
        backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
        kill(0, SIGTERM);
    } 
    else 
    {
        const char msg[] = "\n!!! WORKER CRASH !!!\n=== Stack Trace ===\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        nptrs = backtrace(buffer, MAX_FRAMES);
        backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
    }
    raise(sig);
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
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGPIPE) 실패: %s", strerror(errno));
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGCHLD) 실패: %s", strerror(errno));
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGINT) 실패: %s", strerror(errno));
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGTERM) 실패: %s", strerror(errno));
    struct sigaction sa_crash;
    sa_crash.sa_handler = crash_handler;
    sigemptyset(&sa_crash.sa_mask);
    sa_crash.sa_flags = SA_RESETHAND;
    if (sigaction(SIGSEGV, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGSEGV) 실패: %s", strerror(errno));
    if (sigaction(SIGABRT, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGABRT) 실패: %s", strerror(errno));
    if (sigaction(SIGBUS, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_signal_handlers() : sigaction(SIGBUS) 실패: %s", strerror(errno));
}
void
setup_child_signal_handlers(ServerState *state)
{
    g_state = state;
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;
    if (sigaction(SIGINT, &sa_ignore, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGINT) 무시 설정 실패: %s", strerror(errno));
    if (sigaction(SIGPIPE, &sa_ignore, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGPIPE) 실패: %s", strerror(errno));
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGTERM) 실패: %s", strerror(errno));
    struct sigaction sa_crash;
    sa_crash.sa_handler = crash_handler;
    sigemptyset(&sa_crash.sa_mask);
    sa_crash.sa_flags = SA_RESETHAND;
    if (sigaction(SIGSEGV, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGSEGV) 실패: %s", strerror(errno));
    if (sigaction(SIGABRT, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGABRT) 실패: %s", strerror(errno));
    if (sigaction(SIGBUS, &sa_crash, NULL) == -1)
        log_message(state, LOG_ERROR, "setup_child_signal_handlers() : sigaction(SIGBUS) 실패: %s", strerror(errno));
}
