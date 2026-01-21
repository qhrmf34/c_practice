#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

// Worker 종료 처리
static void shutdown_workers(ServerState *state)
{
    log_message(LOG_INFO, "Worker 종료 시작: %d개", state->worker_count);
    
    int initial_count = state->worker_count;
    pid_t worker_list[MAX_WORKERS];
    for (int i = 0; i < initial_count; i++)
        worker_list[i] = state->worker_pids[i];
    
    for (int i = 0; i < initial_count; i++) 
    {
        if (kill(worker_list[i], SIGTERM) != 0 && errno != ESRCH)
            log_message(LOG_DEBUG, "SIGTERM 전송 실패: PID %d", worker_list[i]);
    }
    
    time_t wait_start = time(NULL);
    while (time(NULL) - wait_start < 5 && state->worker_count > 0)
        usleep(10000);
    
    log_message(LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개", 
                initial_count - state->worker_count, state->worker_count);
    
    if (state->worker_count > 0) 
    {
        log_message(LOG_WARNING, "강제 종료: %d개", state->worker_count);
        for (int i = 0; i < state->worker_count; i++) 
        {
            if (kill(state->worker_pids[i], 0) == 0)
                kill(state->worker_pids[i], SIGKILL);
        }
        usleep(200000);
    }
}

void 
run_server(void)
{
    ServerState state = {0};
    state.running = 1;
    state.start_time = time(NULL);
    state.parent_pid = getpid();
    
    log_init();
    setup_server_signals(&state);
    
    log_message(LOG_INFO, "=== Echo Server 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    int serv_sock = create_server_socket();
    if (serv_sock == -1) 
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        return;
    }
    
    struct pollfd fds[1] = {{.fd = serv_sock, .events = POLLIN, .revents = 0}};
    int session_id = 0;
    
    log_message(LOG_INFO, "클라이언트 대기중");
    
    while (state.running) 
    {
        int poll_ret = do_poll_wait(&fds[0], 1000, "서버 poll");
        if (poll_ret != 0)
            continue; // 타임아웃 또는 에러
        
        if (fds[0].revents & POLLIN) 
        {
            struct sockaddr_in clnt_addr;
            int clnt_sock = safe_accept(serv_sock, &clnt_addr, &state.running);
            
            if (clnt_sock == -1)
                continue; // 종료중
            if (clnt_sock == -2)
                continue; // 재시도
            
            if (!state.running) 
            {
                log_message(LOG_INFO, "종료중 - 새 연결 거부");
                close(clnt_sock);
                continue;
            }
            
            session_id++;
            log_message(LOG_INFO, "연결 수락: %s:%d (Session #%d)", inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), session_id);
            
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state) == -1) 
            {
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
    }
    
    // 종료 처리
    time_t end_time = time(NULL);
    if (state.sigint_received)
        log_message(LOG_INFO, "SIGINT 수신 - 서버 종료");
    else if (state.sigterm_received)
        log_message(LOG_INFO, "SIGTERM 수신 - 서버 종료");
    
    log_message(LOG_INFO, "실행 시간: %ld초", end_time - state.start_time);
    log_message(LOG_INFO, "fork 성공: %d개, 실패: %d개", state.total_forks, state.fork_errors);
    log_message(LOG_INFO, "좀비 회수: %d개", state.zombie_reaped);
    
    shutdown_workers(&state);
    
    if (close(serv_sock) == -1)
        log_message(LOG_ERROR, "close(serv_sock) 실패");
    
    // 최종 좀비 회수
    int final_count = 0;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        final_count++;
    
    log_message(LOG_INFO, "최종 회수: %d개", final_count);
    log_message(LOG_INFO, "=== 서버 종료 완료 ===");
}
