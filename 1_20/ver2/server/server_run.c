#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>

int fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state) {
    pid_t pid;
    char fd_str[32], session_str[32], ip_str[INET_ADDRSTRLEN], port_str[32];

    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL) 
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s", strerror(errno));
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    pid = fork();
    
    if (pid == -1) 
    {
        if (errno == EAGAIN)
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계");
        else
            log_message(LOG_ERROR, "fork() 실패: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) 
    {  // 자식 프로세스
        setup_child_signal_handlers(state);
        close(serv_sock);
        
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1) 
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_GETFD) 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        flags &= ~FD_CLOEXEC;
        if (fcntl(clnt_sock, F_SETFD, flags) == -1) 
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_SETFD) 실패: %s\n", session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        
        char *const argv[] = {"./worker", fd_str, session_str, ip_str, port_str, NULL};
        execvp("./worker", argv);
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s\n", session_id, strerror(errno));
        if (errno == ENOENT)
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음\n", session_id);
        else if (errno == EACCES)
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음\n", session_id);
        
        _exit(127);
    }
    
    state->total_forks++;  // 부모 프로세스
    
    if (state->worker_count < MAX_WORKERS)
        state->worker_pids[state->worker_count++] = pid;
    else
        log_message(LOG_WARNING, "Worker PID 배열 가득참 (MAX_WORKERS=%d)", MAX_WORKERS);
    
    close(clnt_sock);
    
    log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", pid, session_id);
    
    return 0;
}

static void 
handle_child_died(ServerState *state) 
{
    if (!state->child_died) return;
    
    state->child_died = 0;  // flag 리셋
    log_message(LOG_DEBUG, "좀비 회수: %d개, 남은 Worker: %d개", state->zombie_reaped, state->worker_count);
}

static void 
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

static void 
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

void 
run_server(void) 
{
    int serv_sock, clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    int session_id = 0;
    
    ServerState state = {0};  // 서버 상태 초기화
    state.running = 1;
    state.start_time = time(NULL);
    state.parent_pid = getpid();
    
    setup_signal_handlers(&state);
    log_init();
    
    log_message(LOG_INFO, "=== Multi-Process Echo Server (fork-exec) 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    serv_sock = create_server_socket();
    if (serv_sock == -1) 
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        return;
    }
    
    log_message(LOG_INFO, "클라이언트 연결 대기 중 (poll 사용, fork-exec 방식)");
    
    while (state.running) 
    {
        handle_child_died(&state);  // SIGCHLD 처리
        
        int poll_ret = poll_socket(serv_sock, POLLIN, 1000);
        
        if (poll_ret == -2) 
            continue;  // EINTR, 재시도
        if (poll_ret == -1) 
        {
            log_message(LOG_ERROR, "poll() 실패: %s", strerror(errno));
            continue;
        }
        if (poll_ret == 0) 
            continue;  // 타임아웃
        if (poll_ret < 0) 
        {  // 에러 이벤트
            log_message(LOG_ERROR, "poll 에러 이벤트 발생");
            continue;
        }
        
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
        
        if (clnt_sock == -1) 
        {
            if (!state.running) 
            {
                log_message(LOG_INFO, "종료 시그널로 인한 accept() 중단");
                continue;
            }
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                log_message(LOG_DEBUG, "accept() interrupted, 재시도");
                continue;
            }
            log_message(LOG_ERROR, "accept() 실패: %s", strerror(errno));
            continue;
        }
        
        if (!state.running) 
        {
            log_message(LOG_INFO, "종료 중이므로 새 연결 거부");
            close(clnt_sock);
            continue;
        }
        
        session_id++;
        log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d, fd: %d)", inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), session_id, clnt_sock);
        
        int result = fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state);
        
        if (result == -1) 
        {
            close(clnt_sock);
            log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
        }
    }
    
    shutdown_workers(&state);
    
    if (close(serv_sock) == -1)
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    else
        log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    final_cleanup(&state);
}
