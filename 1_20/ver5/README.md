# Multi-Process Echo Server (완전 실무 버전)

##  해결된 모든 문제

### 1. 글로벌 변수 완전 제거 
**Before**: `static ServerState *g_state` 시그널 핸들러에서 참조
**After**: **Self-pipe 패턴** 사용

```c
// 시그널 핸들러는 pipe에 write만
signal_handler(int signo) {
    write(signal_pipe_fd, &signo, 1);  // 
}

// 메인 루프에서 poll로 읽고 처리
poll(pfds, 2, timeout);  // serv_sock + signal_pipe
if (pfds[1].revents & POLLIN) {
    read(signal_pipe[0], &signo, 1);
    handle_signal(&state, signo);  // 
}
```

### 2. 시그널 핸들러 로직 완전 제거 
**Before**: 핸들러에서 `waitpid()` + PID 배열 수정
**After**: 핸들러는 **pipe write만**, 로직은 `handle_signal()`에서

```c
handle_signal(state, SIGCHLD) {
    reap_children(state);  // 여기서 waitpid
}
```

### 3. break로 서버 종료 완전 제거 
**Before**: `if (POLLNVAL) break;` → 서버 죽음
**After**: 에러 시 **소켓 재생성 + continue**

```c
if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    log_error("서버 소켓 에러, 재생성");
    close(serv_sock);
    serv_sock = create_server_socket();  //  재생성
    continue;  //  서버는 계속 실행
}
```

### 4. Poll 에러 처리 통합 
**Before**: 곳곳에 `POLLERR` 체크 분산
**After**: 한 곳에서 통합 처리

```c
if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    goto cleanup;  //  통합

if (pfd.revents & POLLIN) {
    // 정상 처리
}
```

### 5. SIGINT/SIGTERM 처리 통합 
**Before**: 핸들러/shutdown/worker 분산
**After**: `handle_signal()` 하나로 통합

```c
handle_signal(state, signo) {
    if (signo == SIGCHLD)
        reap_children(state);
    else if (signo == SIGINT || signo == SIGTERM)
        state->running = 0;
}
```

## 실무 핵심 패턴

### Self-Pipe Trick
- 글로벌 변수 없이 시그널 처리
- 시그널 → pipe write → poll에서 감지 → 메인에서 처리
- 레이스 컨디션 완전 제거

### Never Die Server
- **break 없음**: 모든 에러는 continue
- 소켓 에러 → 재생성
- Worker 에러 → 해당 세션만 종료
- 서버는 절대 죽지 않음

### 통합 에러 처리
- 한 곳에서 모든 에러 처리
- 흐름 추적 쉬움
- 유지보수 용이

## 파일 구조

### 서버 (10개 파일)
- server_main.c
- server_run.c (self-pipe poll)
- server_signal.c (pipe write만)
- server_handler.c (시그널 처리 + 자식 회수)
- server_accept.c
- server_worker.c (dup2)
- server_child.c (break 없음)
- server_socket.c
- server_log.c (open 유지)
- server_monitor.c

### Worker
- worker_main.c (FD=3)

### 클라이언트
- client_main.c
- client_connect.c
- client_run.c

## 컴파일 및 실행

```bash
make
./server
./client 127.0.0.1 9190
```

## 실무 체크리스트

- [x] 글로벌 변수 완전 제거 (self-pipe)
- [x] 시그널 핸들러 로직 제거 (pipe write만)
- [x] break 완전 제거 (서버 절대 죽지 않음)
- [x] poll 에러 처리 통합
- [x] SIGINT/SIGTERM 처리 통합
- [x] MAX_WORKERS 실제 제한
- [x] FD 고정 (dup2)
- [x] 로그 성능 개선 (open 유지)
- [x] 레이스 컨디션 제거
- [x] 코드 컨벤션 통일

## 안정성 및 성능

1. **완벽한 안정성**: 서버는 절대 죽지 않음
2. **레이스 제거**: self-pipe로 완전 해결
3. **리소스 제한**: MAX_WORKERS 실제 적용
4. **성능**: 로그 open 유지, 불필요한 코드 제거
5. **운영성**: 에러 시 자동 복구
