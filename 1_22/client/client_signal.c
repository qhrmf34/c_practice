#include "client_function.h"
static ClientState *g_client_state = NULL;
static void 
signal_handler(int signo)
{
    int saved_errno = errno;
    if (signo == SIGINT) 
    {
        if (g_client_state)
            g_client_state->running = 0;
        const char msg[] = "\n[클라이언트] 종료 시그널 수신\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
    errno = saved_errno;
}
void 
setup_client_signal_handlers(ClientState *state)
{
    g_client_state = state;
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        fprintf(stderr, "setup_client_signal_handlers() : sigaction(SIGINT) 실패: %s\n", strerror(errno));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1)
        fprintf(stderr, "setup_client_signal_handlers() : sigaction(SIGPIPE) 실패: %s\n", strerror(errno));
}
