#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <string.h>

/*
 * 시그널 처리 (메인 루프에서 호출)
 * 
 * @param state: 서버 상태
 * @param signo: 시그널 번호
 * @param log_ctx: 로그 컨텍스트
 * 
 * 처리 시그널:
 * - SIGCHLD: 자식 프로세스 종료 → reap_children() 호출
 * - SIGINT: Ctrl+C → running=0 (graceful shutdown)
 * - SIGTERM: kill 명령 → running=0 (graceful shutdown)
 * 
 * Self-Pipe 패턴:
 * - 시그널 핸들러에서는 pipe write만
 * - 이 함수에서 실제 로직 처리 (메인 스레드, async-safe 불필요)
 */
void
handle_signal(ServerState *state, int signo, LogContext *log_ctx)
{
    if (signo == SIGCHLD) {
        // 자식 프로세스 회수 (좀비 방지)
        reap_children(state, log_ctx);
    } else if (signo == SIGINT || signo == SIGTERM) {
        // 부모 프로세스만 종료 처리
        if (getpid() == state->parent_pid) {
            log_message(log_ctx, LOG_INFO, "종료 시그널 수신: %s", 
                       signo == SIGINT ? "SIGINT(Ctrl+C)" : "SIGTERM(kill)");
            state->running = 0;  // 메인 루프 종료 플래그
        }
    }
}

/*
 * 좀비 프로세스 회수
 * 
 * @param state: 서버 상태 (worker_pids 배열 포함)
 * @param log_ctx: 로그 컨텍스트
 * 
 * 동작:
 * 1. waitpid(WNOHANG)로 종료된 자식 모두 회수
 * 2. PID 배열에서 해당 Worker 제거 (swap-delete)
 * 3. worker_count 감소
 * 4. zombie_reaped 증가 (통계)
 * 
 * WNOHANG:
 * - Non-blocking으로 종료된 자식만 즉시 회수
 * - 실행 중인 자식은 건드리지 않음
 */
void
reap_children(ServerState *state, LogContext *log_ctx)
{
    pid_t pid;
    int status;
    
    // 종료된 자식 프로세스를 모두 회수 (WNOHANG = non-blocking)
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        state->zombie_reaped++;
        
        // PID 배열에서 해당 Worker 제거 (swap-delete 기법)
        for (int i = 0; i < state->worker_count; i++) 
        {
            if (state->worker_pids[i] == pid) 
            {
                // 마지막 요소와 교체 후 count 감소 (O(1) 삭제)
                state->worker_pids[i] = state->worker_pids[state->worker_count - 1];
                state->worker_count--;
                break;
            }
        }
    }
    
    log_message(log_ctx, LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", 
               state->zombie_reaped, state->worker_count);
}

/*
 * 모든 Worker 종료 (Graceful Shutdown)
 * 
 * @param state: 서버 상태
 * @param log_ctx: 로그 컨텍스트
 * 
 * 단계:
 * 1. 모든 Worker에게 SIGTERM 전송 (graceful shutdown 요청)
 * 2. 5초 대기 (Worker들이 정상 종료할 시간)
 * 3. 남은 Worker에게 SIGKILL 전송 (강제 종료)
 * 4. 최종 좀비 회수
 * 
 * SIGTERM vs SIGKILL:
 * - SIGTERM: Worker가 현재 작업 마무리 후 종료 (graceful)
 * - SIGKILL: 즉시 강제 종료 (ungraceful, 최후 수단)
 */
void
shutdown_all_workers(ServerState *state, LogContext *log_ctx)
{
    time_t end_time = time(NULL);
    
    // 통계 출력
    log_message(log_ctx, LOG_INFO, "총 실행 시간: %ld초", end_time - state->start_time);
    log_message(log_ctx, LOG_INFO, "성공한 fork: %d개", state->total_forks);
    log_message(log_ctx, LOG_INFO, "회수한 좀비: %d개", state->zombie_reaped);
    log_message(log_ctx, LOG_INFO, "활성 Worker %d개에게 SIGTERM 전송", state->worker_count);
    
    // Worker 목록 복사 (reap_children이 배열을 수정하므로)
    int initial_count = state->worker_count;
    pid_t worker_list[MAX_WORKERS];
    memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));
    
    // [1단계] SIGTERM 전송 (Graceful Shutdown 요청)
    for (int i = 0; i < initial_count; i++) 
    {
        if (kill(worker_list[i], SIGTERM) == 0)
            log_message(log_ctx, LOG_DEBUG, "SIGTERM 전송 성공: PID %d", worker_list[i]);
    }
    
    // [2단계] 5초 대기 (Worker들이 정상 종료할 시간)
    log_message(log_ctx, LOG_INFO, "Worker 정상 종료 대기 (최대 5초)");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5) 
    {
        reap_children(state, log_ctx);  // 종료된 Worker 회수
        if (state->worker_count == 0) 
        {
            log_message(log_ctx, LOG_INFO, "모든 Worker 정상 종료 완료");
            return;
        }
        usleep(10000);  // 10ms 대기 (CPU 부하 감소)
    }
    
    // [3단계] 남은 Worker 강제 종료 (SIGKILL)
    if (state->worker_count > 0) 
    {
        log_message(log_ctx, LOG_WARNING, "남은 Worker %d개 강제 종료 (SIGKILL)", state->worker_count);
        
        int kill_count = state->worker_count;
        memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));
        
        for (int i = 0; i < kill_count; i++) 
        {
            // 프로세스가 아직 존재하는지 확인 (kill(pid, 0))
            if (kill(worker_list[i], 0) == 0) 
            {
                log_message(log_ctx, LOG_WARNING, "SIGKILL 전송: PID %d", worker_list[i]);
                kill(worker_list[i], SIGKILL);
            }
        }
        usleep(200000);  // 200ms 대기 (커널이 처리할 시간)
        reap_children(state, log_ctx);  // 최종 좀비 회수
    }
    
    log_message(log_ctx, LOG_INFO, "종료 완료: 남은 Worker %d개", state->worker_count);
}
