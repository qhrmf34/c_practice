# Multi-Process Echo Server (실무 최종 버전)

## 핵심 개선 사항

### 1. SIGCHLD 레이스 컨디션 해결 
**문제**: 시그널 핸들러에서 PID 배열 직접 수정 → 데이터 깨짐
**해결**: 핸들러는 `child_died=1` flag만 설정, waitpid/배열 정리는 메인 루프에서

```c
// 시그널 핸들러
if (signo == SIGCHLD)
    g_state->child_died = 1;  // flag만

// 메인 루프
handle_child_died(&state);  // 여기서 waitpid + 배열 정리
```

### 2. MAX_WORKERS 제한 실제로 걸기 
**문제**: 배열 꽉 차도 fork 계속 됨 → shutdown 시 kill 못함
**해결**: fork 전에 체크해서 연결 거부

```c
if (state->worker_count >= MAX_WORKERS) {
    log_message(LOG_WARNING, "최대 Worker 수 도달, 연결 거부");
    return -1;
}
```

### 3. dup2(fd, 3)로 Worker FD 고정 
**문제**: argv로 fd 넘기고 파싱/검증 코드 복잡
**해결**: 무조건 FD=3 사용

```c
// 부모
dup2(clnt_sock, 3);
exec("./worker")

// Worker
int client_sock = 3;  // 항상 고정
getpeername(3, ...)   // IP/port는 여기서
```

### 4. 로그 성능 개선 
**문제**: 매번 fopen/fclose 반복 → 성능 저하
**해결**: 파일 유지, write() 사용

```c
static int log_fd = -1;

log_init() {
    log_fd = open(LOG_FILE, O_WRONLY | O_APPEND);
}

log_message() {
    write(log_fd, log_line, len);  // fopen/fclose 없음
}
```

### 5. 스레드 안전성 개선 
- `inet_ntoa()` → `inet_ntop()` (스레드 안전)

## 파일 구조

### 서버 (10개 파일)
- **server_main.c**: 메인 함수
- **server_run.c**: 메인 루프 (간결)
- **server_accept.c**: accept 처리
- **server_worker.c**: worker 생성/관리 + SIGCHLD 정리
- **server_shutdown.c**: 종료 처리
- **server_child.c**: 자식 프로세스 로직
- **server_socket.c**: 소켓 생성
- **server_log.c**: 로깅 (성능 개선)
- **server_monitor.c**: 리소스 모니터링
- **server_signal.c**: 시그널 핸들러 (flag만)

### Worker
- **worker_main.c**: FD=3 고정, 간소화

### 클라이언트 (3개 파일)
- **client_main.c**
- **client_connect.c**
- **client_run.c**

## 컴파일 및 실행

```bash
make           # 컴파일
./server       # 서버 실행
./client 127.0.0.1 9190  # 클라이언트
make clean     # 정리
```

## 실무 체크리스트

- [x] SIGCHLD 레이스 컨디션 해결
- [x] MAX_WORKERS 실제 제한
- [x] FD 고정 (dup2)
- [x] 로그 성능 개선
- [x] 스레드 안전 함수 사용
- [x] 코드 컨벤션 통일
- [x] 기능별 파일 분리
- [x] 시그널 핸들러 통합

## 성능 및 안정성

1. **레이스 컨디션 제거**: 시그널 핸들러에서 공유 데이터 최소 접근
2. **리소스 제한**: 최대 10,000개 동시 접속 (설정 가능)
3. **로그 성능**: fopen/fclose 제거로 I/O 부하 감소
4. **메모리 효율**: 불필요한 검증 코드 제거
5. **운영 안정성**: 실무 표준 패턴 적용
