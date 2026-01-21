#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/*
 * Self-Pipe 패턴용 글로벌 변수
 * 
 * 시그널 핸들러는 함수 파라미터를 받을 수 없으므로
 * pipe의 write FD만 글로벌로 유지
 * 
 * 시그널 핸들러에서는 이 FD에 write만 수행 (async-safe)
 */
static int g_signal_pipe_fd = -1;

/*
 * Worker용 글로벌 포인터
 * 
 * Worker 프로세스의 SIGTERM 핸들러에서 접근
 * 핸들러는 running flag만 0으로 설정 (async-safe)
 */
static WorkerContext *g_worker_ctx = NULL;

/*
 * 클라이언트용 글로벌 포인터
 * 
 * 클라이언트의 SIGINT 핸들러에서 접근
 * 핸들러는 running flag만 0으로 설정 (async-safe)
 */
static ClientContext *g_client_ctx = NULL;

/*
 * 서버 시그널 핸들러
 * 
 * Self-Pipe 패턴:
 * 1. 시그널 발생
 * 2. 핸들러가 pipe에 시그널 번호 write
 * 3. 메인 루프의 poll이 pipe readable 감지
 * 4. 메인 루프에서 시그널 번호 read하고 처리
 * 
 * 장점:
 * - 핸들러는 write만 수행 (async-safe)
 * - 실제 로직은 메인 루프에서 안전하게 처리
 * - 레이스 컨디션 완전 제거
 */
static void
signal_handler(int signo)
{
    int saved_errno = errno;  // errno 보존 (POSIX 요구사항)
    unsigned char sig = (unsigned char)signo;
    
    // pipe에 시그널 번호 write (async-safe)
    if (g_signal_pipe_fd >= 0)
        write(g_signal_pipe_fd, &sig, 1);
    
    errno = saved_errno;  // errno 복원
}

/*
 * Worker 시그널 핸들러
 * 
 * SIGTERM 수신 시 running flag를 0으로 설정
 * Worker 메인 루프가 flag 확인 후 graceful shutdown
 */
static void
worker_signal_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    
    if (g_worker_ctx)
        g_worker_ctx->running = 0;  // flag만 설정 (async-safe)
    
    errno = saved_errno;
}

/*
 * 클라이언트 시그널 핸들러
 * 
 * SIGINT(Ctrl+C) 수신 시 running flag를 0으로 설정
 * 클라이언트 메인 루프가 flag 확인 후 graceful shutdown
 */
static void
client_signal_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    
    if (g_client_ctx)
        g_client_ctx->running = 0;  // flag만 설정 (async-safe)
    
    errno = saved_errno;
}

/*
 * 서버 시그널 핸들러 설정 (Self-Pipe 패턴)
 * 
 * @param state: 서버 상태 (signal_pipe 포함)
 * 
 * 처리 시그널:
 * - SIGCHLD: 자식 프로세스 종료
 * - SIGINT: Ctrl+C
 * - SIGTERM: kill 명령
 * - SIGPIPE: 무시 (EPIPE로 처리)
 */
void
setup_signal_handlers(ServerState *state)
{
    // Self-Pipe 생성
    if (pipe(state->signal_pipe) == -1) {
        fprintf(stderr, "pipe() 실패: %s\n", strerror(errno));
        return;
    }
    
    // Non-blocking 설정 (핸들러에서 write가 block되지 않도록)
    fcntl(state->signal_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(state->signal_pipe[1], F_SETFL, O_NONBLOCK);
    
    // 글로벌 변수에 write FD 저장 (핸들러에서 사용)
    g_signal_pipe_fd = state->signal_pipe[1];
    
    // 시그널 핸들러 등록
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // 일부 시스템콜 자동 재시작
    
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // SIGPIPE 무시 (write 시 EPIPE 에러로 처리)
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/*
 * Worker 시그널 핸들러 설정
 * 
 * @param ctx: Worker 컨텍스트 (running flag 포함)
 * 
 * 처리 시그널:
 * - SIGINT: 무시 (부모만 처리)
 * - SIGTERM: Graceful shutdown
 * - SIGPIPE: 무시
 */
void
setup_worker_signal_handlers(WorkerContext *ctx)
{
    g_worker_ctx = ctx;  // 글로벌 포인터 설정
    
    signal(SIGINT, SIG_IGN);    // SIGINT는 부모만 처리
    signal(SIGPIPE, SIG_IGN);   // SIGPIPE 무시
    
    // SIGTERM 핸들러 등록
    struct sigaction sa;
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
}

/*
 * 클라이언트 시그널 핸들러 설정
 * 
 * @param ctx: 클라이언트 컨텍스트 (running flag 포함)
 * 
 * 처리 시그널:
 * - SIGINT: Graceful shutdown
 * - SIGPIPE: 무시
 */
void
setup_client_signal_handlers(ClientContext *ctx)
{
    g_client_ctx = ctx;  // 글로벌 포인터 설정
    
    signal(SIGPIPE, SIG_IGN);  // SIGPIPE 무시
    
    // SIGINT 핸들러 등록
    struct sigaction sa;
    sa.sa_handler = client_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}
