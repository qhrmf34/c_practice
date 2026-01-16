#include "server.h"

static int total_forks = 0;
static int total_reaped = 0;

/* 종료 시그널 핸들러 */
static void
shutdown_handler(int signo)
{
    (void)signo;
    g_server_running = 0;
    log_info("[서버] 종료 시그널 수신");
}


/* SIGCHLD 핸들러 */
static void
sigchld_handler(int signo)
{
    (void)signo;
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        total_reaped++;
        log_info("[SIGCHLD] 자식 회수: PID=%d (총 %d개)", pid, total_reaped);
    }
}
/* 시그널 핸들러 설정 */
void
setup_signal_handlers(void)
{
    struct sigaction sa;
    
    /* SIGCHLD (waitpid 모드일 때만) */
    if (g_use_waitpid) {
        sa.sa_handler = sigchld_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        if (sigaction(SIGCHLD, &sa, NULL) < 0) {
            error_exit("sigaction(SIGCHLD)");
        }
        log_info("[설정] SIGCHLD 핸들러 등록");
    }
    
    /* SIGINT, SIGTERM */
    sa.sa_handler = shutdown_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* fork 통계 업데이트 */
void
increment_fork_count(void)
{
    total_forks++;
}

int
get_fork_count(void)
{
    return total_forks;
}

int
get_reaped_count(void)
{
    return total_reaped;
}

/* 서버 통계 출력 */
void
print_server_statistics(void)
{
    printf("\n========== 서버 종료 통계 ==========\n");
    printf("생성된 자식:   %d 개\n", total_forks);
    printf("회수된 자식:   %d 개\n", total_reaped);
    printf("미회수:        %d 개\n", total_forks - total_reaped);
    printf("====================================\n\n");
}