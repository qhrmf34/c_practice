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
#define MAX_FRAMES 64

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
    volatile sig_atomic_t running;  // 서버/워커 실행 플래그
    volatile sig_atomic_t child_died;  // SIGCHLD 플래그
    pid_t worker_pids[MAX_WORKERS];  // 워커 PID 배열
    int worker_count;  // 현재 워커 수
    int total_forks;  // 총 fork 횟수
    int zombie_reaped;  // 회수된 좀비 수
    time_t start_time;  // 시작 시간
    pid_t parent_pid;  // 부모 PID
} ServerState;

// 서버 함수
int 
create_server_socket(void);
void 
run_server(void);
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state);
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);

// 리소스 모니터링
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

// 시그널 핸들러
void 
setup_signal_handlers(ServerState *state);
void 
setup_child_signal_handlers(ServerState *state);

// 공통 poll 함수
int 
poll_socket(int sock, short events, int timeout);

#endif
