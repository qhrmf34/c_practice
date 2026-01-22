#include "server_function.h"
#include <execinfo.h>

static ServerState *g_state = NULL;
static void 
signal_handler(int signo)
{
    int saved_errno = errno;                        // 기존 에러 번호 보존
    if (signo == SIGCHLD)                           // 자식 종료 시 부모에게 알림 플래그 설정
    {
        if (g_state)
            g_state->child_died = 1;
    } 
    else if (signo == SIGINT || signo == SIGTERM)   //부모에게 SIGINT신호 들어올경우 종료플래그로 while문 벗어나며 shutdown_handler실행(자식 종료) 
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
    struct sigaction sa_pipe;       // 파이프 에러(SIGPIPE) 무시 설정
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
