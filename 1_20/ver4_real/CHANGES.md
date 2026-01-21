# 실무 개선 사항 요약

## 심각한 버그 수정

### 1. SIGCHLD 레이스 컨디션 (Critical) 
**Before**:
```c
// 시그널 핸들러에서 직접 배열 수정
signal_handler(SIGCHLD) {
    waitpid(...);
    worker_pids[i] = ...;  // ❌ 위험!
    worker_count--;        // ❌ 레이스!
}
```

**After**:
```c
// 핸들러는 flag만
signal_handler(SIGCHLD) {
    g_state->child_died = 1;  //  안전
}

// 메인에서 처리
handle_child_died() {
    waitpid(...);
    worker_pids[i] = ...;  //  안전
}
```

### 2. MAX_WORKERS 제한 안 걸림 (Critical) 
**Before**:
```c
// 배열 꽉 차도 fork 계속
if (worker_count < MAX_WORKERS)
    worker_pids[worker_count++] = pid;
else
    log_warning("가득참");  // ❌ fork는 계속!
```

**After**:
```c
// fork 전에 체크
if (worker_count >= MAX_WORKERS) {
    log_warning("최대 도달, 연결 거부");
    return -1;  //  fork 안 함
}
```

## 주요 개선 사항

### 3. Worker FD 고정 (dup2) 
**Before**: argv로 fd/ip/port 넘기고 복잡한 파싱/검증 (100줄+)
**After**: dup2(fd, 3)로 고정, getpeername() 사용 (30줄)

### 4. 로그 성능 개선 
**Before**: 매번 fopen/fclose → 느림
**After**: open 유지, write() 사용 → 빠름

### 5. 스레드 안전성 
**Before**: inet_ntoa() (스레드 unsafe)
**After**: inet_ntop() (스레드 safe)

## 테스트 시나리오

1. **동시 접속 1만개** → MAX_WORKERS로 제한 확인
2. **SIGCHLD 폭주** → 레이스 없이 안정적으로 회수
3. **로그 부하 테스트** → 성능 개선 확인
4. **장시간 운영** → 메모리 누수 없음

## 파일 변경 내역

- server_signal.c: SIGCHLD flag만
- server_worker.c: handle_child_died + MAX_WORKERS 체크 + dup2
- worker_main.c: FD=3 고정, 간소화
- server_log.c: open 유지, write 사용
- server_child.c: inet_ntop 사용
- server_run.c: inet_ntop 사용
