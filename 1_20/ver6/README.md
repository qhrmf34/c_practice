# Multi-Process Echo Server (완전 실무 버전)

##  글로벌 변수 최소화 완료

### 구조체 기반 설계

모든 상태를 구조체로 관리:

```c
/* 서버 상태 - 부모 프로세스 */
typedef struct {
    int running;                   // 서버 실행 플래그
    pid_t worker_pids[MAX_WORKERS];// 워커 PID 배열
    int worker_count;              // 현재 워커 수
    int signal_pipe[2];            // self-pipe FD
    int log_fd;                    // 로그 파일 FD (글로벌 제거)
    // ...
} ServerState;

/* 워커 상태 - 자식 프로세스 */
typedef struct {
    volatile sig_atomic_t running; // 워커 실행 플래그
    int session_id;                // 세션 ID
    pid_t worker_pid;              // 워커 PID
} WorkerContext;

/* 클라이언트 상태 */
typedef struct {
    volatile sig_atomic_t running; // 클라이언트 실행 플래그
    int client_id;                 // 클라이언트 ID
} ClientContext;

/* 로그 상태 */
typedef struct {
    int fd;                        // 로그 파일 FD
    int console_enabled;           // 콘솔 출력 여부
} LogContext;
```

### 시그널 핸들러의 불가피한 글로벌

**시그널 핸들러는 파라미터를 받을 수 없기 때문에** 최소한의 글로벌 포인터가 필요합니다.

```c
/*
 * 시그널 핸들러용 글로벌 포인터 (최소화)
 * 
 * 이유:
 * 1. signal(SIGTERM, handler)는 handler에 파라미터 전달 불가
 * 2. async-signal-safe 제약으로 복잡한 로직 불가
 * 3. 포인터만 글로벌, 실제 데이터는 구조체에 있음
 * 
 * 실무 표준:
 * - 시그널 핸들러에서 최소한의 글로벌 사용은 acceptable
 * - 핸들러는 flag 수정만, 실제 로직은 메인 루프에서 처리
 */
static int g_signal_pipe_fd = -1;      // self-pipe write FD
static WorkerContext *g_worker_ctx;     // 워커 컨텍스트 포인터
static ClientContext *g_client_ctx;     // 클라이언트 컨텍스트 포인터
```

이 3개의 글로벌은:
-  모두 **구조체를 가리키는 포인터** (데이터는 구조체에)
-  **시그널 핸들러 제약** 때문에 불가피
-  **실무에서도 표준 패턴**

## 핵심 설계 패턴

### 1. Self-Pipe 패턴 (글로벌 제거)

```c
// [1] 초기화
ServerState state;
pipe(state.signal_pipe);  // 구조체에 저장

// [2] 글로벌 포인터 최소화
g_signal_pipe_fd = state.signal_pipe[1];  // write FD만

// [3] 시그널 핸들러 (async-safe)
static void signal_handler(int signo) {
    write(g_signal_pipe_fd, &signo, 1);  // pipe write만
}

// [4] 메인 루프에서 처리
poll(pfds, 2, timeout);  // signal_pipe[0] + serv_sock
if (pfds[1].revents & POLLIN) {
    read(state.signal_pipe[0], &signo, 1);
    handle_signal(&state, signo);  // 구조체 전달
}
```

### 2. Worker 상태 관리

```c
// 워커 생성 시
WorkerContext ctx = {.running = 1, .session_id = session_id};
setup_worker_signal_handlers(&ctx);  // 글로벌 포인터 설정

// 시그널 핸들러에서
static void worker_signal_handler(int signo) {
    if (g_worker_ctx)
        g_worker_ctx->running = 0;  // flag만 수정
}

// 메인 루프에서
while (ctx.running) {  // flag 체크
    // I/O 처리
}
```

### 3. 로그 시스템 (글로벌 제거)

```c
// Before: static int log_fd = -1;  ❌ 글로벌
// After: ServerState에 포함          구조체

ServerState state = {.log_fd = -1};
log_init(&state);  // state.log_fd 초기화
log_message(&state, LOG_INFO, "메시지");  // state 전달
```

## 파일 구조

### 서버 (10개 파일)
- **server_main.c**: main() 함수만
- **server_run.c**: 메인 루프 (self-pipe poll)
- **server_signal.c**: 시그널 핸들러 (최소 글로벌)
- **server_handler.c**: 시그널 처리 로직
- **server_accept.c**: accept() 래퍼
- **server_worker.c**: fork-exec (dup2)
- **server_child.c**: 워커 I/O 로직
- **server_socket.c**: 소켓 생성
- **server_log.c**: 로그 (구조체 기반)
- **server_monitor.c**: 리소스 모니터링

### 워커
- **worker_main.c**: FD=3 고정

### 클라이언트 (3개 파일)
- **client_main.c**
- **client_connect.c**
- **client_run.c**

## 컴파일 및 실행

```bash
# 컴파일
make

# 서버 실행
./server

# 클라이언트 실행 (다른 터미널)
./client 127.0.0.1 9190

# 정리
make clean
```

## 실무 체크리스트

- [x] **글로벌 변수 최소화** (시그널 핸들러용 3개만)
- [x] **구조체 기반 설계** (모든 상태를 구조체로)
- [x] **Self-Pipe 패턴** (레이스 컨디션 제거)
- [x] **Never Die 원칙** (break 없음, 소켓 재생성)
- [x] **MAX_WORKERS 제한** (리소스 보호)
- [x] **FD 고정 (dup2)** (워커 코드 간소화)
- [x] **로그 성능** (open 유지, write 사용)
- [x] **상세한 주석** (모든 구조체/함수 설명)

## 왜 이 3개의 글로벌은 OK?

### 실무 관점

1. **시그널 핸들러 제약**
   - `signal(SIGTERM, handler)`는 핸들러에 파라미터 전달 불가
   - 전역 변수 or 전역 포인터만 접근 가능

2. **최소화 달성**
   - 포인터만 글로벌, 실제 데이터는 구조체
   - 핸들러는 flag 수정만 (async-safe)
   - 실제 로직은 메인 루프에서 처리

3. **업계 표준**
   - NGINX, Apache 등도 유사한 패턴 사용
   - "완벽한 글로벌 제거"보다 "실용적 최소화"가 목표

### 대안이 없는 이유

```c
// ❌ 불가능: 시그널 핸들러에 파라미터 전달
signal(SIGTERM, handler_with_context(ctx));  // 불가능

//  유일한 방법: 글로벌 포인터
static Context *g_ctx;
g_ctx = &ctx;
signal(SIGTERM, handler);  // 핸들러에서 g_ctx 접근
```

## 결론

이 코드는:
-  **최대한 글로벌 제거** (3개만 남음)
-  **모두 구조체 포인터** (데이터는 구조체에)
-  **실무 표준 패턴** (Self-Pipe + 최소 글로벌)
-  **상세한 주석** (모든 결정의 이유 설명)

시그널 핸들러의 제약 때문에 완벽한 글로벌 제거는 불가능하지만, 
**실무에서 가능한 최선의 설계**를 달성했습니다.
