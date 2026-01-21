#define _POSIX_C_SOURCE 200809L
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
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - state->start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", state->total_forks);
    log_message(LOG_INFO, "회수한 좀비: %d개", state->zombie_reaped);
    log_message(LOG_INFO, "활성 Worker에게 SIGTERM 전송");
    
    int initial_count = state->worker_count;
    pid_t worker_list[MAX_WORKERS];
    memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));
    
    log_message(LOG_INFO, "총 %d개 Worker에게 SIGTERM 전송 시작", initial_count);
    
    for (int i = 0; i < initial_count; i++) 
    {
        if (kill(worker_list[i], SIGTERM) == 0)
            log_message(LOG_DEBUG, "SIGTERM 전송 성공: PID %d", worker_list[i]);
        else if (errno != ESRCH)
            log_message(LOG_DEBUG, "SIGTERM 전송 실패: PID %d (%s)", worker_list[i], strerror(errno));
    }
    
    log_message(LOG_INFO, "Worker 정상 종료 대기 중 (최대 5초)...");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5) 
    {
        handle_child_died(state); //여기서 실행해야 worker_count가 줄어듬
        if (state->worker_count == 0) 
        {
            log_message(LOG_INFO, "모든 Worker 정상 종료 완료");
            return;
        }
        usleep(10000);
    }
    
    int graceful_exits = initial_count - state->worker_count;
    log_message(LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개", graceful_exits, state->worker_count);
    
    if (state->worker_count > 0) 
    {
        log_message(LOG_WARNING, "남은 Worker %d개 강제 종료 (SIGKILL)", state->worker_count);
        
        int kill_count = state->worker_count;
        pid_t kill_list[MAX_WORKERS];
        memcpy(kill_list, state->worker_pids, state->worker_count * sizeof(pid_t));
        
        for (int i = 0; i < kill_count; i++) 
        {
            if (kill(kill_list[i], 0) == 0) 
            {
                log_message(LOG_WARNING, "SIGKILL 전송: PID %d", kill_list[i]);
                kill(kill_list[i], SIGKILL);
            }
        }
        usleep(200000);
    }
}

void
final_cleanup(ServerState *state)
{
    log_message(LOG_INFO, "최종 좀비 회수 중...");
    int final_count = 0;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        final_count++;
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
            log_message(LOG_DEBUG, "SIGKILL로 종료된 Worker 회수: PID %d", pid);
    }
    
    if (pid == -1 && errno != ECHILD)
        log_message(LOG_ERROR, "waitpid() 에러: %s", strerror(errno));
    
    log_message(LOG_INFO, "최종 회수: %d개", final_count);
    log_message(LOG_INFO, "총 회수된 좀비: %d개", state->zombie_reaped + final_count);
    log_message(LOG_INFO, "남은 활성 Worker: %d개", state->worker_count);
    
    if (state->worker_count > 0)
        log_message(LOG_WARNING, "경고: 회수하지 못한 Worker가 있을 수 있음");
    
    print_resource_limits();
    log_message(LOG_INFO, "=== 서버 정상 종료 완료 ===");
}
