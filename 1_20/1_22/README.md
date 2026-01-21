# Multi-Process Echo Server - 개선 버전

## 주요 변경 사항

### 1. 프로세스 그룹 관리 추가

#### run_server.c
- `setpgid(0, 0)`: 부모 프로세스를 프로세스 그룹 리더로 설정
- 부모 PID가 프로세스 그룹 ID(PGID)가 됨
- 이후 생성되는 모든 워커는 이 그룹에 편입

#### fork_and_exec_worker.c
- 자식 프로세스에서 `setpgid(0, state->parent_pid)`: 자식을 부모 그룹에 편입
- 부모 프로세스에서도 `setpgid(pid, state->parent_pid)`: race condition 대비
- `EACCES` 에러는 정상적인 레이스 케이스로 무시

#### shutdown.c
- 기존: `for` 루프로 개별 `kill(pid, SIGTERM)`
- 변경: `killpg(pgid, SIGTERM)` 한 번으로 전체 그룹에 시그널 전송
- 강제 종료 시에도 `killpg(pgid, SIGKILL)` 사용
- 코드 단순화 및 성능 향상

### 2. 전역 변수 제거

#### 로그 시스템 (log.c)
- 기존: `static int log_fd = -1;`
- 변경: `LogContext` 구조체로 캡슐화
- `ServerState`에 `LogContext *log_ctx` 포인터 추가
- 모든 `log_message()` 호출에 컨텍스트 전달

#### 시그널 핸들러 (signal.c)
- `static ServerState *g_state`: 유지 (async-signal-safe 제약으로 불가피)
- 시그널 핸들러는 전역 변수 없이 함수 인자를 받을 수 없음
- 주석으로 이유 명시

#### 클라이언트 (client_function.c)
- `static ClientState *g_client_state`: 유지 (시그널 핸들러용)
- 시그널 핸들러 제약과 동일한 이유

### 3. 상세한 에러 처리 주석 추가

모든 시스템 콜과 에러 처리에 주석 추가:
- 각 에러가 발생할 수 있는 구체적인 상황 설명
- 예시: `socket() 실패: fd 한계, 권한 없음, 메모리 부족`
- 예시: `bind() 실패: EADDRINUSE (포트 사용 중), EACCES (권한 없음)`
- 예시: `fork() 실패: EAGAIN (프로세스 리소스 한계)`

### 4. 코드 구조 개선

#### 파일 분리
- `server_main.c`: 서버 진입점 (간단)
- `worker_main.c`: 워커 진입점 (exec 후 실행)
- `client_main.c`: 클라이언트 진입점
- 각 진입점을 별도 파일로 분리하여 관심사 분리

#### 함수 시그니처 변경
- `log_message(LogContext *ctx, ...)`: 컨텍스트 전달
- `log_init(LogContext *ctx)`: 초기화
- `log_close(LogContext *ctx)`: 정리

## 빌드 및 실행

```bash
# 빌드
make clean
make

# 서버 실행
./server

# 클라이언트 실행 (다른 터미널)
./client 127.0.0.1 9190

# 종료
Ctrl+C
```

## 구조체 관계도

```
ServerState
├── running (sig_atomic_t)
├── child_died (sig_atomic_t)
├── worker_pids[MAX_WORKERS]
├── worker_count
├── total_forks
├── zombie_reaped
├── start_time
├── parent_pid (프로세스 그룹 ID)
└── log_ctx ──→ LogContext
                 └── fd (로그 파일 디스크립터)
```

## 주요 시스템 콜 에러 처리

### socket()
- EMFILE: 프로세스당 fd 한계
- ENFILE: 시스템 전체 fd 한계
- EACCES: 권한 없음
- ENOMEM: 메모리 부족

### bind()
- EADDRINUSE: 포트 이미 사용 중 (가장 흔함)
- EACCES: 권한 없음 (1024 이하 포트)
- EINVAL: 잘못된 주소

### accept()
- EINTR: 시그널로 중단 (재시도 가능)
- EAGAIN/EWOULDBLOCK: 일시적 불가 (재시도 가능)
- EMFILE: fd 한계

### fork()
- EAGAIN: 프로세스 리소스 한계 (ulimit)
- ENOMEM: 메모리 부족

### read()/write()
- EINTR: 시그널 중단 (재시도)
- EAGAIN/EWOULDBLOCK: 일시적 불가 (재시도)
- EPIPE: 연결 끊김 (write 시)
- EOF (0): 정상 종료 (read 시)

### poll()
- EINTR: 시그널 중단 (재시도)
- POLLERR: 소켓 오류
- POLLHUP: 연결 끊김
- POLLNVAL: 잘못된 fd

### killpg()
- ESRCH: 프로세스 그룹 없음 (모두 종료됨)
- EPERM: 권한 없음

## 프로세스 그룹 관리의 장점

1. **간단한 종료 처리**: 개별 kill 대신 killpg 한 번
2. **레이스 방지**: fork 직후 부모/자식 모두 setpgid 시도
3. **안전한 시그널 전송**: 그룹 단위로 SIGTERM, SIGKILL 전송
4. **코드 단순화**: PID 배열 순회 불필요

## 전역 변수 최소화의 장점

1. **재진입 가능성**: 함수가 전역 상태에 의존하지 않음
2. **테스트 용이성**: 컨텍스트를 주입하여 테스트 가능
3. **명확한 의존성**: 함수 시그니처로 의존성 표현
4. **스레드 안전성**: 향후 멀티스레드 전환 시 유리

## 주의사항

- 시그널 핸들러는 async-signal-safe 제약으로 전역 변수 불가피
- `g_state`와 `g_client_state`는 시그널 핸들러용으로 유지
- 로그 파일은 부모만 열고, 워커는 stdout만 사용
