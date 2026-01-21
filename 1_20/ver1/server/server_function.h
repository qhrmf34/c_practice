#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_WORKERS 10000
#define LOG_FILE "server.log"
#define IO_TARGET 10
#define POLL_TIMEOUT 1000
#define SESSION_IDLE_TIMEOUT 60

// 세션 상태
typedef 
enum 
{
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

// 로그 레벨
typedef 
enum 
{
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;

// Session Descriptor
typedef 
struct 
{
    int sock;
    struct sockaddr_in addr;
    int session_id;
    SessionState state;
    int io_count;
    time_t start_time;
    time_t last_activity;
} SessionDescriptor;

// 리소스 모니터
typedef 
struct 
{
    int active_sessions;
    int total_sessions;
    long heap_usage;
    int open_fds;
    time_t start_time;
} ResourceMonitor;

// 서버 상태 관리
typedef 
struct 
{
    volatile sig_atomic_t running;
    volatile sig_atomic_t sigint_received;
    volatile sig_atomic_t sigterm_received;
    volatile sig_atomic_t sigchld_received;
    int total_forks;
    int fork_errors;
    int zombie_reaped;
    time_t start_time;
    pid_t parent_pid;
    pid_t worker_pids[MAX_WORKERS];
    int worker_count;
} ServerState;

// Worker 상태
typedef 
struct 
{
    volatile sig_atomic_t shutdown_requested;
} WorkerState;

// 서버 함수
int 
create_server_socket(void);
void 
run_server(void);
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);

// Worker 함수
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, WorkerState *wstate);

// 시그널 핸들러
void 
setup_server_signals(ServerState *state);
void 
setup_worker_signals(WorkerState *wstate);

// 리소스 모니터
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

// 로그
void 
log_message(LogLevel level, const char* format, ...);
void 
log_init(void);

// 에러 처리
int 
handle_poll_error(int revents, const char *context);
int 
handle_io_error(int error_num, const char *operation);

// 공통 I/O 함수
int 
do_poll_wait(struct pollfd *pfd, int timeout, const char *context);
ssize_t 
safe_read(int fd, void *buf, size_t count, SessionDescriptor *session);
ssize_t 
safe_write_all(int fd, const void *buf, size_t count, SessionDescriptor *session);
int 
check_session_timeout(SessionDescriptor *session);
void 
update_session_activity(SessionDescriptor *session);

// 서버 헬퍼
int 
safe_accept(int serv_sock, struct sockaddr_in *clnt_addr, volatile sig_atomic_t *running);

#endif
