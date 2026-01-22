#define _XOPEN_SOURCE 500
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

void
shutdown_workers(ServerState *state)
{
    time_t end_time = time(NULL);
    pid_t pgid = state->pgid;                                              /* 프로세스 그룹 ID (부모 PID) */
    
    log_message(state->log_ctx, LOG_INFO, "서버 종료 시그널 수신");
    log_message(state->log_ctx, LOG_INFO, "총 실행 시간: %ld초", end_time - state->start_time);
    log_message(state->log_ctx, LOG_INFO, "성공한 fork: %d개", state->total_forks);
    log_message(state->log_ctx, LOG_INFO, "회수한 좀비: %d개", state->zombie_reaped);
    log_message(state->log_ctx, LOG_INFO, "프로세스 그룹(PGID=%d)에 SIGTERM 전송", (int)pgid);
    
    int initial_count = state->worker_count;
    
    if (state->use_killpg)     /* killpg 사용 가능 여부에 따라 분기 */
    {
        pid_t pgid = state->pgid;
        log_message(state->log_ctx, LOG_INFO, "프로세스 그룹(PGID=%d)에 SIGTERM 전송", (int)pgid);
        
        if (killpg(pgid, SIGTERM) == -1)                                         /* killpg 실패 가능: 그룹 없음, 권한 없음 */
        {
            if (errno == ESRCH)                                                  /* 프로세스 그룹이 없음 (모두 종료됨) */
                log_message(state->log_ctx, LOG_DEBUG, "프로세스 그룹 없음 (이미 종료)");
            else
                log_message(state->log_ctx, LOG_ERROR, "killpg(SIGTERM) 실패: %s", strerror(errno));
        }
        else
            log_message(state->log_ctx, LOG_INFO, "SIGTERM 전송 완료");
    }
    else 
    {
        log_message(state->log_ctx, LOG_INFO, "개별 Worker %d개에 SIGTERM 전송 (killpg 미사용)", initial_count); /* fallback: 개별 PID에 kill */
        
        pid_t worker_list[MAX_WORKERS];
        memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));  /* 배열 복사 */
        
        for (int i = 0; i < initial_count; i++) 
        {
            if (kill(worker_list[i], SIGTERM) == 0)                              /* 개별 kill */
                log_message(state->log_ctx, LOG_DEBUG, "SIGTERM 전송 성공: PID %d", worker_list[i]);
            else if (errno != ESRCH)                                             /* ESRCH: 프로세스 없음 (정상) */
                log_message(state->log_ctx, LOG_DEBUG, "SIGTERM 전송 실패: PID %d (%s)", worker_list[i], strerror(errno));
        }
    }
    
    log_message(state->log_ctx, LOG_INFO, "Worker 정상 종료 대기 중 (최대 5초)...");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5)     /* 5초간 정상 종료 대기 */
    {
        handle_child_died(state);                                                /* 종료된 자식 회수 (worker_count 감소) */
        if (state->worker_count == 0)                                            /* 모든 워커 종료 완료 */
        {
            log_message(state->log_ctx, LOG_INFO, "모든 Worker 정상 종료 완료");
            return;
        }
        usleep(10000);                                                           /* 10ms 대기 */
    }
    
    int graceful_exits = initial_count - state->worker_count;
    log_message(state->log_ctx, LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개", graceful_exits, state->worker_count);
    
    if (state->worker_count > 0)     /* 5초 내 종료하지 않은 워커는 강제 종료 */
    {
        log_message(state->log_ctx, LOG_WARNING, "남은 Worker %d개 강제 종료 (SIGKILL)", state->worker_count);
        
        if (state->use_killpg) 
        {
            pid_t pgid = state->pgid;
            if (killpg(pgid, SIGKILL) == -1)                                     /* killpg로 강제 종료 */
            {
                if (errno == ESRCH)
                    log_message(state->log_ctx, LOG_DEBUG, "프로세스 그룹 없음 (이미 종료)");
                else
                    log_message(state->log_ctx, LOG_ERROR, "killpg(SIGKILL) 실패: %s", strerror(errno));
            }
            else
                log_message(state->log_ctx, LOG_WARNING, "SIGKILL 전송 완료");
        }
        else 
        {
            int kill_count = state->worker_count;            /* fallback: 개별 SIGKILL */
            pid_t kill_list[MAX_WORKERS];
            memcpy(kill_list, state->worker_pids, state->worker_count * sizeof(pid_t));
            
            for (int i = 0; i < kill_count; i++) 
            {
                if (kill(kill_list[i], 0) == 0)                                  /* 프로세스 존재 확인 */
                {
                    log_message(state->log_ctx, LOG_WARNING, "SIGKILL 전송: PID %d", kill_list[i]);
                    kill(kill_list[i], SIGKILL);
                }
            }
        }
        usleep(200000);
    }      
}

void
final_cleanup(ServerState *state)
{
    log_message(state->log_ctx, LOG_INFO, "최종 좀비 회수 중...");
    int final_count = 0;
    pid_t pid;
    int status;
    
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)                            /* 남은 모든 좀비 프로세스 회수 WNOHANG: 비블로킹 회수 */
    {
        final_count++;
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)                  /* SIGKILL로 종료된 경우 */
            log_message(state->log_ctx, LOG_DEBUG, "SIGKILL로 종료된 Worker 회수: PID %d", pid);
    }
    
    if (pid == -1 && errno != ECHILD)                                            /* waitpid 에러 (ECHILD는 정상: 자식 없음) */
        log_message(state->log_ctx, LOG_ERROR, "waitpid() 에러: %s", strerror(errno));
    
    log_message(state->log_ctx, LOG_INFO, "최종 회수: %d개", final_count);
    log_message(state->log_ctx, LOG_INFO, "총 회수된 좀비: %d개", state->zombie_reaped + final_count);
    log_message(state->log_ctx, LOG_INFO, "남은 활성 Worker: %d개", state->worker_count);
    
    if (state->worker_count > 0)                                                 /* 회수 못한 워커 경고 */
        log_message(state->log_ctx, LOG_WARNING, "경고: 회수하지 못한 Worker가 있을 수 있음");
    
    print_resource_limits();
    log_message(state->log_ctx, LOG_INFO, "=== 서버 정상 종료 완료 ===");
}
