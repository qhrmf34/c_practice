#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <string.h>

#define MAX_FRAMES 64

static pid_t g_parent_pid = 0;

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
    signal(sig, SIG_DFL); //원래 시그널 동작으로 복귀 후 재발생 - 자식프로세스
    raise(sig);
}

// 부모용 설정
void 
setup_parent_signal_handlers(void)
{
    g_parent_pid = getpid();
    
    signal(SIGSEGV, signal_crash_handler);
    signal(SIGABRT, signal_crash_handler);
    signal(SIGBUS, signal_crash_handler);
}

// 자식용 설정
void setup_child_signal_handlers(void)
{
    signal(SIGSEGV, signal_crash_handler);
    signal(SIGABRT, signal_crash_handler);
    signal(SIGBUS, signal_crash_handler);
}