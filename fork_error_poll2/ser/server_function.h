#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_SESSIONS 100  // 자식 프로세스당 최대 세션 수

// 세션 상태
typedef enum {
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

// Session Descriptor - 각 클라이언트 세션 정보
typedef struct {
    int sock;                      // 클라이언트 소켓
    struct sockaddr_in addr;       // 클라이언트 주소
    int session_id;                // 세션 ID
    pthread_t thread_id;           // 처리 스레드 ID
    SessionState state;            // 세션 상태
    int io_count;                  // 처리한 I/O 횟수
    time_t start_time;             // 세션 시작 시간
    time_t last_activity;          // 마지막 활동 시간
} SessionDescriptor;

// 리소스 모니터링 정보
typedef struct {
    int active_sessions;           // 활성 세션 수
    int total_sessions;            // 총 처리한 세션 수
    long heap_usage;               // 힙 메모리 사용량 (bytes)
    int open_fds;                  // 열린 파일 디스크립터 수
    time_t start_time;             // 프로세스 시작 시간
} ResourceMonitor;

// 클라이언트 처리 함수 (스레드에서 실행)
void* handle_client_thread(void *arg);

// 서버 소켓 생성 및 설정
int create_server_socket(void);

// 서버 실행 (메인 루프 - poll 기반)
void run_server(void);

// 자식 프로세스 메인 함수
void child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr);

// 시스템 리소스 출력
void print_resource_limits(void);

// 리소스 모니터링 함수
void monitor_resources(ResourceMonitor *monitor);
void print_resource_status(ResourceMonitor *monitor);

// 힙 메모리 사용량 체크
long get_heap_usage(void);

// 열린 파일 디스크립터 수 체크
int count_open_fds(void);

#endif