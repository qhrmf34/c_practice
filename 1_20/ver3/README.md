# Multi-Process Echo Server (실무 스타일)

## 주요 특징

### 1. 실무 스타일 코드 컨벤션
```c
int
function_name(int arg)
{
    if (condition)
        single_statement;
    else {
        multiple;
        statements;
    }
}
```

### 2. 실무 스타일 poll 처리
- poll 함수로 감싸지 않고 인라인으로 에러 처리
- 에러 우선 처리 후 정상 이벤트 처리
```c
int ret = poll(&pfd, 1, timeout);
if (ret == -1) {
    if (errno == EINTR) continue;
    // 에러 처리
}
if (ret == 0) continue;  // timeout

// 에러 이벤트 먼저 체크
if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    // 에러 처리
    continue;
}

// 정상 이벤트 처리
if (pfd.revents & POLLIN) {
    // 읽기 처리
}
```

### 3. 기능별 파일 분리
- **server_main.c**: 메인 함수만
- **server_run.c**: 메인 루프 (간결하게)
- **server_accept.c**: accept 처리
- **server_worker.c**: worker 생성 및 관리
- **server_shutdown.c**: 종료 처리
- **server_child.c**: 자식 프로세스 로직
- **server_socket.c**: 소켓 생성
- **server_log.c**: 로깅
- **server_monitor.c**: 리소스 모니터링
- **server_signal.c**: 시그널 핸들러

### 4. 시그널 핸들러 통합
```c
static void signal_handler(int signo) {
    if (signo == SIGCHLD) {
        // 자식 종료 처리
    } else if (signo == SIGINT || signo == SIGTERM) {
        // 서버 종료 처리
    }
}
```

## 파일 구조

### 서버 (10개 파일)
- server_main.c
- server_run.c (메인 루프만)
- server_accept.c (accept 전담)
- server_worker.c (worker 관리)
- server_shutdown.c (종료 처리)
- server_child.c (자식 로직)
- server_socket.c
- server_log.c
- server_monitor.c
- server_signal.c

### Worker
- worker_main.c

### 클라이언트 (3개 파일)
- client_main.c
- client_connect.c
- client_run.c

## 컴파일 및 실행

```bash
make           # 컴파일
./server       # 서버 실행
./client 127.0.0.1 9190  # 클라이언트
make clean     # 정리
```
