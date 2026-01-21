#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_WORKERS 10000
#define LOG_FILE "server.log"
#define IO_TARGET 10
#define POLL_TIMEOUT 1000
#define SESSION_IDLE_TIMEOUT 60

typedef enum {
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

typedef enum {
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;

typedef struct {
    int sock;
    struct sockaddr_in addr;
    int session_id;
    SessionState state;
    int io_count;
    time_t start_time;
    time_t last_activity;
} SessionDescriptor;

typedef struct {
    int active_sessions;
    int total_sessions;
    long heap_usage;
    int open_fds;
    time_t start_time;
} ResourceMonitor;

typedef struct {
    int fd;                                      /* 로그 파일 디스크립터 */
} LogContext;

typedef struct {
    volatile sig_atomic_t running;               /* 서버 실행 상태 플래그 */
    volatile sig_atomic_t child_died;            /* SIGCHLD 수신 플래그 */
    pid_t worker_pids[MAX_WORKERS];              /* Worker PID 배열 */
    int worker_count;                            /* 현재 활성 Worker 수 */
    int total_forks;                             /* 총 fork 횟수 */
    int zombie_reaped;                           /* 회수한 좀비 프로세스 수 */
    time_t start_time;                           /* 서버 시작 시각 */
    pid_t parent_pid;                            /* 부모 프로세스 PID (PGID) */
    pid_t pgid;                                  /* 실제 프로세스 그룹 ID */
    int use_killpg;                              /* killpg 사용 가능 여부 (1=가능, 0=fallback) */
    LogContext *log_ctx;                         /* 로그 컨텍스트 포인터 */
} ServerState;

void 
run_server(void);
int 
create_server_socket(void);
int 
accept_client(int serv_sock, struct sockaddr_in *clnt_addr, ServerState *state);
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);
void 
handle_child_died(ServerState *state);
void 
shutdown_workers(ServerState *state);
void 
final_cleanup(ServerState *state);
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state);
void 
print_resource_limits(void);
void 
monitor_resources(ResourceMonitor *monitor);
void 
print_resource_status(ResourceMonitor *monitor);
long 
get_heap_usage(void);
int 
count_open_fds(void);
void 
log_message(LogContext *ctx, LogLevel level, const char* format, ...);
void 
log_init(LogContext *ctx);
void 
log_close(LogContext *ctx);
void 
setup_signal_handlers(ServerState *state);
void 
setup_child_signal_handlers(ServerState *state);

#endif
