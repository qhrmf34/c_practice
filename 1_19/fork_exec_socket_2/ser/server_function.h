#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_SESSIONS 100
#define LOG_FILE "server.log"

// 세션 상태
typedef enum 
{
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

//로그
typedef enum
{
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;

// Session Descriptor - thread_id 제거
typedef struct 
{
    int sock;                      // 클라이언트 소켓
    struct sockaddr_in addr;       // 클라이언트 주소
    int session_id;                // 세션 ID
    SessionState state;            // 세션 상태
    int io_count;                  // 처리한 I/O 횟수
    time_t start_time;             // 세션 시작 시간
    time_t last_activity;          // 마지막 활동 시간
} SessionDescriptor;

// 리소스 모니터링 정보
typedef struct 
{
    int active_sessions;           // 활성 세션 수 (항상 0 또는 1)
    int total_sessions;            // 총 처리한 세션 수 (항상 1)
    long heap_usage;               // 힙 메모리 사용량 (bytes)
    int open_fds;                  // 열린 파일 디스크립터 수
    time_t start_time;             // 프로세스 시작 시간
} ResourceMonitor;

// 함수 선언
int 
create_server_socket(void);

void 
run_server(void);

void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr);

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

// 로그 함수 선언
void 
log_message(LogLevel level, const char* format, ...);

void 
log_init(void);  // 로그 파일 초기화 (옵션)


//Stack trace 함수 선언
void 
setup_parent_signal_handlers(void);
void 
setup_child_signal_handlers(void);
void 
signal_crash_handler(int sig);

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