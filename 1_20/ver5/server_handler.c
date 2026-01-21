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

void
handle_signal(ServerState *state, int signo)
{
    if (signo == SIGCHLD) {
        reap_children(state);
    } else if (signo == SIGINT || signo == SIGTERM) {
        if (getpid() == state->parent_pid) {
            log_message(LOG_INFO, "종료 시그널 수신: %s", signo == SIGINT ? "SIGINT" : "SIGTERM");
            state->running = 0;
        }
    }
}

void
reap_children(ServerState *state)
{
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        state->zombie_reaped++;
        
        for (int i = 0; i < state->worker_count; i++) {
            if (state->worker_pids[i] == pid) {
                state->worker_pids[i] = state->worker_pids[state->worker_count - 1];
                state->worker_count--;
                break;
            }
        }
    }
    
    log_message(LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", state->zombie_reaped, state->worker_count);
}

void
shutdown_all_workers(ServerState *state)
{
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - state->start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", state->total_forks);
    log_message(LOG_INFO, "회수한 좀비: %d개", state->zombie_reaped);
    log_message(LOG_INFO, "활성 Worker %d개에게 SIGTERM 전송", state->worker_count);
    
    int initial_count = state->worker_count;
    pid_t worker_list[MAX_WORKERS];
    memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));
    
    for (int i = 0; i < initial_count; i++) {
        if (kill(worker_list[i], SIGTERM) == 0)
            log_message(LOG_DEBUG, "SIGTERM 전송: PID %d", worker_list[i]);
    }
    
    log_message(LOG_INFO, "Worker 정상 종료 대기 (최대 5초)");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5) {
        reap_children(state);
        if (state->worker_count == 0) {
            log_message(LOG_INFO, "모든 Worker 정상 종료");
            return;
        }
        usleep(10000);
    }
    
    if (state->worker_count > 0) {
        log_message(LOG_WARNING, "남은 Worker %d개 강제 종료 (SIGKILL)", state->worker_count);
        
        int kill_count = state->worker_count;
        memcpy(worker_list, state->worker_pids, state->worker_count * sizeof(pid_t));
        
        for (int i = 0; i < kill_count; i++) {
            if (kill(worker_list[i], 0) == 0)
                kill(worker_list[i], SIGKILL);
        }
        usleep(200000);
        reap_children(state);
    }
    
    log_message(LOG_INFO, "종료 완료: 남은 Worker %d개", state->worker_count);
}
