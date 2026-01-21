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

/* 세션 상태 */
typedef enum {
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

/* 로그 레벨 */
typedef enum {
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;

/* 세션 디스크립터 - 각 클라이언트 연결 정보 */
typedef struct {
    int sock;                      // 클라이언트 소켓 FD
    struct sockaddr_in addr;       // 클라이언트 주소
    int session_id;                // 세션 ID
    SessionState state;            // 현재 세션 상태
    int io_count;                  // 완료한 I/O 횟수
    time_t start_time;             // 세션 시작 시간
    time_t last_activity;          // 마지막 활동 시간 (타임아웃 체크용)
} SessionDescriptor;

/* 리소스 모니터링 정보 */
typedef struct {
    int active_sessions;           // 현재 활성 세션 수
    int total_sessions;            // 총 처리한 세션 수
    long heap_usage;               // 힙 메모리 사용량 (bytes)
    int open_fds;                  // 열린 파일 디스크립터 수
    time_t start_time;             // 프로세스 시작 시간
} ResourceMonitor;

/* 서버 상태 - 메인 프로세스에서 사용 */
typedef struct {
    int running;                   // 서버 실행 플래그 (0=종료, 1=실행)
    pid_t worker_pids[MAX_WORKERS]; // Worker PID 배열
    int worker_count;              // 현재 활성 Worker 수
    int total_forks;               // 총 fork 횟수 (통계)
    int zombie_reaped;             // 회수한 좀비 프로세스 수 (통계)
    time_t start_time;             // 서버 시작 시간
    pid_t parent_pid;              // 부모 프로세스 PID (시그널 처리 구분용)
    int signal_pipe[2];            // Self-pipe: [0]=read, [1]=write
} ServerState;

/* Worker 컨텍스트 - Worker 프로세스에서 사용 */
typedef struct {
    volatile sig_atomic_t running; // Worker 실행 플래그 (SIGTERM에서 수정)
    int session_id;                // 이 Worker의 세션 ID
    pid_t worker_pid;              // 이 Worker의 PID
} WorkerContext;

/* 로그 컨텍스트 - 로그 시스템에서 사용 */
typedef struct {
    int fd;                        // 로그 파일 FD (-1이면 미초기화)
    int console_enabled;           // 콘솔 출력 여부
} LogContext;

/* 클라이언트 컨텍스트 - 클라이언트에서 사용 */
typedef struct {
    volatile sig_atomic_t running; // 클라이언트 실행 플래그
    int client_id;                 // 클라이언트 ID
} ClientContext;

/* server_run.c */
void 
run_server(void);

/* server_socket.c */
int 
create_server_socket(void);

/* server_accept.c */
int 
accept_client(int serv_sock, struct sockaddr_in *clnt_addr);

/* server_worker.c */
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, 
                         struct sockaddr_in *clnt_addr, ServerState *state, LogContext *log_ctx);

/* server_handler.c */
void 
handle_signal(ServerState *state, int signo, LogContext *log_ctx);
void 
reap_children(ServerState *state, LogContext *log_ctx);
void 
shutdown_all_workers(ServerState *state, LogContext *log_ctx);

/* server_child.c */
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, 
                       WorkerContext *ctx, LogContext *log_ctx);

/* server_monitor.c */
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

/* server_log.c */
void 
log_message(LogContext *ctx, LogLevel level, const char* format, ...);
void 
log_init(LogContext *ctx);
void 
log_cleanup(LogContext *ctx);

/* server_signal.c */
void 
setup_signal_handlers(ServerState *state);
void 
setup_worker_signal_handlers(WorkerContext *ctx);
void 
setup_client_signal_handlers(ClientContext *ctx);

#endif
