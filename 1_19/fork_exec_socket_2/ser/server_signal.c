#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <string.h>

#define MAX_FRAMES 64

static pid_t g_parent_pid = 0;

// === async-signal-safe safe_write ===
static void 
safe_write(const char *msg)
{
    // strlen 직접 계산 (async-signal-safe)
    size_t len = 0;
    while (msg[len]) len++;
    (void)write(STDERR_FILENO, msg, len);
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
    if (getpid() == g_parent_pid && g_parent_pid != 0)
    {
        // 부모 프로세스
        safe_write("\n!!! PARENT CRASH !!!\n");
        print_trace();
        kill(0, SIGTERM);  // 모든 자식 종료
    }
    else
    {
        // 자식 프로세스 (Worker)
        safe_write("\n!!! WORKER CRASH !!!\n");
        print_trace();
    }
    raise(sig); //SIGSEGV재발생 -> 커널이 해당 프로세스 강제 종료
}

// 부모용 설정
void 
setup_parent_signal_handlers(void)
{
    g_parent_pid = getpid();
    
    struct sigaction sa;
    sa.sa_handler = signal_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // 한 번 실행 후 기본 동작으로 -> 왜 죽었는지 OS와 부모에게 알리기 위해
    
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

// 자식용 설정 (exec 전, exec 후 모두 사용)
void 
setup_child_signal_handlers(void)
{
    // g_parent_pid는 설정하지 않음
    // exec 전: 부모에서 상속받은 값 유지
    // exec 후: 0 (새 프로그램)
    struct sigaction sa;
    sa.sa_handler = signal_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    
}