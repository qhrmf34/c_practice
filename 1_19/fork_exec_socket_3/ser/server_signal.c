#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <string.h>

#define MAX_FRAMES 64

static pid_t g_parent_pid = 0;

// Worker의 graceful shutdown 플래그
volatile sig_atomic_t worker_shutdown_requested = 0;

// 시그널 핸들러는 비동기 안정성때문에 write사용 ! (동기 상황에 sigsegv걸렸을때 여기서도 동기로 진행되면 데드락)
static void 
safe_write(const char *msg)
{
    write(STDERR_FILENO, msg, strlen(msg));
}

// Stack trace 출력
static void 
print_trace(void)
{
    void *buffer[MAX_FRAMES];
    int nptrs = backtrace(buffer, MAX_FRAMES);
    
    safe_write("\n=== Stack Trace ===\n");
    backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
}

// SIGTERM handler (worker용 graceful shutdown)
static void
sigterm_handler(int sig)
{
    (void)sig;
    worker_shutdown_requested = 1;
    safe_write("\n[Worker] SIGTERM 수신, graceful shutdown 시작\n");
}

// Crash handler
void 
signal_crash_handler(int sig)
{
    if (getpid() == g_parent_pid)
    {
        // 부모
        safe_write("\n!!! PARENT CRASH !!!\n");
        print_trace();
        kill(0, SIGTERM);  // 모든 자식 종료
    }
    else
    {
        // 자식
        safe_write("\n!!! CHILD CRASH !!!\n");
        print_trace();
    }
    
    // 원래 시그널 동작으로 복귀 후 재발생
    struct sigaction sa_default;
    sa_default.sa_handler = SIG_DFL;
    sigemptyset(&sa_default.sa_mask);
    sa_default.sa_flags = 0;
    sigaction(sig, &sa_default, NULL);
    
    raise(sig);
}

// 부모용 설정
void 
setup_parent_signal_handlers(void)
{
    g_parent_pid = getpid();
    
    struct sigaction sa;
    sa.sa_handler = signal_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // 한 번만 실행되도록
    
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);   // 추가: Floating Point Exception
    sigaction(SIGILL, &sa, NULL);   // 추가: Illegal Instruction
}

// 자식용 설정
void 
setup_child_signal_handlers(void)
{
    struct sigaction sa;
    
    // Crash handler
    sa.sa_handler = signal_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // 한 번만 실행되도록
    
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    
    // SIGTERM handler (graceful shutdown)
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // RESETHAND 제거 (여러 번 받을 수 있음)
    
    sigaction(SIGTERM, &sa, NULL);
}