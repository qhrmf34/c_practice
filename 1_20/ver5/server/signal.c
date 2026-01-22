#define _XOPEN_SOURCE 500
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#define MAX_FRAMES 64
/* 
 * 시그널 핸들러는 async-signal-safe 요구사항으로 인해
 * 전역 변수 사용이 불가피함 (함수 인자 전달 불가)
 */
static ServerState *g_state = NULL;

static void
signal_handler(int signo)
{
    int saved_errno = errno;                                                     /* 시그널 핸들러는 errno를 보존해야 함 */
    
    if (signo == SIGCHLD)                                                        /* 자식 프로세스 종료 시그널 */
    {
        if (g_state)
            g_state->child_died = 1;                                             /* 플래그만 설정, waitpid는 메인 루프에서 */
    }
    else if (signo == SIGINT || signo == SIGTERM)                                /* 종료 시그널 (Ctrl+C 또는 kill) */
    {
        if (g_state && getpid() == g_state->parent_pid)                          /* 부모 프로세스만 종료 플래그 설정 */
            g_state->running = 0;
        else if (g_state)                                                        /* 자식 프로세스도 종료 플래그 설정 */
            g_state->running = 0;
    }
    errno = saved_errno;                                                         /* errno 복원 */
}

static void
crash_handler(int sig)
{
    void *buffer[MAX_FRAMES];
    int nptrs;
    
    if (g_state && getpid() == g_state->parent_pid)                              /* 부모 프로세스 크래시 */
    {
        const char msg[] = "\n!!! PARENT CRASH !!!\n=== Stack Trace ===\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);                              /* async-signal-safe 출력 */
        
        nptrs = backtrace(buffer, MAX_FRAMES);                                   /* 스택 트레이스 수집 */
        backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);                      /* async-signal-safe 출력 */
        
        if (g_state->use_killpg && g_state->pgid > 0)                            /* 프로세스 그룹으로 종료 가능하면 */
            killpg(g_state->pgid, SIGTERM);                                      /* 모든 자식 종료 */
        else
            kill(0, SIGTERM);                                                    /* fallback: 세션 내 모든 프로세스 종료 */
    }
    else                                                                         /* 자식 프로세스 (Worker) 크래시 */
    {
        const char msg[] = "\n!!! WORKER CRASH !!!\n=== Stack Trace ===\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        
        nptrs = backtrace(buffer, MAX_FRAMES);
        backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
    }
    
    raise(sig);                                                                  /* SIGSEGV 재발생 -> 커널이 프로세스 강제 종료, 코어 덤프 생성 */
}
void
setup_signal_handlers(ServerState *state)
{
    g_state = state;                                                             /* 시그널 핸들러가 접근할 수 있도록 전역 설정 */
    
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;                                                /* SIGPIPE 무시 (끊긴 소켓 write 시 발생) */
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)                                /* 실패 가능: 시스템 제한, 메모리 부족 */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGPIPE) 실패: %s", strerror(errno));
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;                                     /* 시스템 콜 자동 재시작, SIGCHLD는 종료 시에만 */
    if (sigaction(SIGCHLD, &sa, NULL) == -1)                                     /* 실패 가능: 시스템 제한 */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGCHLD) 실패: %s", strerror(errno));
    
    sa.sa_flags = 0;                                                             /* SIGINT/SIGTERM은 재시작 안 함 (즉시 종료) */
    if (sigaction(SIGINT, &sa, NULL) == -1)                                      /* 실패 가능: 시스템 제한 */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGINT) 실패: %s", strerror(errno));
    if (sigaction(SIGTERM, &sa, NULL) == -1)                                     /* 실패 가능: 시스템 제한 */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGTERM) 실패: %s", strerror(errno));
    
    /* 크래시 핸들러 설정 (부모 프로세스) */
    struct sigaction sa_crash;
    sa_crash.sa_handler = crash_handler;
    sigemptyset(&sa_crash.sa_mask);
    sa_crash.sa_flags = SA_RESETHAND;                                            /* 한 번 실행 후 기본 동작으로 (OS에 크래시 알림) */
    
    if (sigaction(SIGSEGV, &sa_crash, NULL) == -1)                               /* Segmentation fault */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGSEGV) 실패: %s", strerror(errno));
    if (sigaction(SIGABRT, &sa_crash, NULL) == -1)                               /* Abort signal (assert 실패 등) */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGABRT) 실패: %s", strerror(errno));
    if (sigaction(SIGBUS, &sa_crash, NULL) == -1)                                /* Bus error (잘못된 메모리 접근) */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGBUS) 실패: %s", strerror(errno));
}

void
setup_child_signal_handlers(ServerState *state)
{
    g_state = state;                                                             /* 자식도 전역 상태 설정 */
    
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;

    if (sigaction(SIGINT, &sa_ignore, NULL) == -1)                               /* 자식은 SIGINT 무시 (부모만 처리) */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGINT) 무시 설정 실패: %s", strerror(errno));

    if (sigaction(SIGPIPE, &sa_ignore, NULL) == -1)                              /* SIGPIPE 무시 (끊긴 소켓 write 방어) */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGPIPE) 실패: %s", strerror(errno));

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1)                                     /* SIGTERM은 처리 (graceful shutdown) */
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGTERM) 실패: %s", strerror(errno));
    
    /* 크래시 핸들러 설정 (자식 프로세스) */
    struct sigaction sa_crash;
    sa_crash.sa_handler = crash_handler;
    sigemptyset(&sa_crash.sa_mask);
    sa_crash.sa_flags = SA_RESETHAND;                                            /* 한 번 실행 후 기본 동작으로 */
    
    if (sigaction(SIGSEGV, &sa_crash, NULL) == -1)
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGSEGV) 실패: %s", strerror(errno));
    if (sigaction(SIGABRT, &sa_crash, NULL) == -1)
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGABRT) 실패: %s", strerror(errno));
    if (sigaction(SIGBUS, &sa_crash, NULL) == -1)
        log_message(state->log_ctx, LOG_ERROR, "sigaction(SIGBUS) 실패: %s", strerror(errno));
}
