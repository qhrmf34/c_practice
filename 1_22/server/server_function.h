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
run_server(void);                                                               // 서버 전체 실행
int 
create_server_socket(void);                                                     // 리스닝 소켓 생성
int 
accept_client(int serv_sock, struct sockaddr_in *clnt_addr, ServerState *state);// accept 래퍼(EINTR/EAGAIN 처리), 성공 시 client FD / 실패 -1
int 
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);  // fork+exec로 워커 생성, PID/카운트 관리, 성공 0 / 실패 -1
void 
handle_child_died(ServerState *state);                                          // SIGCHLD 플래그 확인 후 waitpid(WNOHANG)로 좀비 회수 + PID목록/worker_count 갱신
void 
shutdown_workers(ServerState *state);                                           // 서버 종료 시 워커들에게 SIGTERM→대기→SIGKILL(가능하면 killpg, 아니면 PID별 kill)
void 
final_cleanup(ServerState *state);                                              // 종료 직전 남은 좀비 최종 회수 + 통계/리소스 정보 출력
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state);// 워커가 클라이언트 1명을 처리하는 메인( poll/read/write/타임아웃/종료 )
void 
print_resource_limits(void);                                                    // 시스템 리소스 한계(RLIMIT_*) 출력(디버그/운영 확인용)
void 
monitor_resources(ResourceMonitor *monitor);                                    // 현재 프로세스의 리소스(힙/FD 등) 측정해서 monitor에 채움
void 
print_resource_status(ResourceMonitor *monitor);                                // monitor에 담긴 리소스 상태를 보기 좋게 출력
long 
get_heap_usage(void);                                                           // 힙 사용량 추정(가능하면 mallinfo2), 실패/미지원이면 0 또는 대체값
int 
count_open_fds(void);                                                           // 열린 FD 개수(/proc/<pid>/fd 스캔), 실패 시 -1
void 
log_message(LogContext *ctx, LogLevel level, const char* format, ...);          // 시간/레벨/PID 붙여 stdout + (있으면) 로그파일에 기록
void 
log_init(LogContext *ctx);                                                      // 로그 파일 open + FD_CLOEXEC 설정 + 시작 헤더 기록
void 
log_close(LogContext *ctx);                                                     // 로그 종료 푸터 기록 후 close
void 
setup_signal_handlers(ServerState *state);                                      // 부모용 시그널 설정(SIGCHLD/SIGINT/SIGTERM/SIGPIPE 등)
void 
setup_child_signal_handlers(ServerState *state);                                // 자식/워커용 시그널 설정(보통 SIGINT 무시, SIGTERM 처리, SIGPIPE 무시)

//테스트 함수들
void 
test_segfault(void);
void 
test_abort(void);
void 
test_division_by_zero(void);
void 
test_crash_with_stack(void);

#endif
