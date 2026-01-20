#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>

// 전역 변수
static volatile sig_atomic_t server_running = 1;
static volatile sig_atomic_t zombie_reaped = 0;
static int total_forks = 0;
static int total_execs = 0;
static int fork_errors = 0;
static int exec_errors = 0;
static int rejected_connections = 0;
static time_t start_time;
static pid_t g_parent_pid = 0;

// Worker PID 추적
#define MAX_WORKERS 1024
static pid_t worker_pids[MAX_WORKERS];
static int worker_count = 0;

// 시스템 제한
static int system_max_workers = MAX_WORKERS;

// === async-signal-safe 헬퍼 함수들 ===
static void 
safe_write(const char *msg)
{
    size_t len = 0;
    while (msg[len]) len++;
    write(STDERR_FILENO, msg, len);
}

static void
safe_write_int(int num)
{
    char buf[32];
    int i = 0;
    int is_negative = 0;
    
    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }
    if (num == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while (num > 0)
        {
            buf[i++] = '0' + (num % 10);
            num /= 10;
        }
    }
    if (is_negative)
    {
        buf[i++] = '-';
    }
    while (i > 0)
    {
        write(STDERR_FILENO, &buf[--i], 1);
    }
}

// SIGCHLD 핸들러 - 좀비 회수만 (O(1))
static void 
sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        zombie_reaped++;
        
        if (WIFEXITED(status)) 
        {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 127) 
            {
                safe_write("[SIGCHLD] Worker exec 실패 (PID: ");
                safe_write_int(pid);
                safe_write(")\n");
            } 
            else if (exit_code > 0) 
            {
                safe_write("[SIGCHLD] Worker 에러 종료 (PID: ");
                safe_write_int(pid);
                safe_write(")\n");
            }
        }
        else if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            safe_write("[SIGCHLD] Worker 시그널 종료 (PID: ");
            safe_write_int(pid);
            safe_write(", 시그널: ");
            safe_write_int(sig);
            safe_write(")\n");
        }
    }
    errno = saved_errno;
}

static void 
shutdown_handler(int signo)
{
    (void)signo;
    if (getpid() != g_parent_pid)
    {
        return;
    }
    server_running = 0;
}

// Worker 정리 (메인 컨텍스트에서만)
static void
cleanup_dead_workers(void)
{
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    for (int i = 0; i < worker_count; i++)
    {
        if (kill(worker_pids[i], 0) == -1 && errno == ESRCH)
        {
            log_message(LOG_DEBUG, "정리: PID %d 제거 (종료됨)", worker_pids[i]);
            worker_pids[i] = worker_pids[worker_count - 1];
            worker_count--;
            i--;
        }
    }
    
    sigprocmask(SIG_SETMASK, &old_set, NULL);
}

// 시스템 제한 확인
static void
check_system_limits(void)
{
    struct rlimit rlim;
    
    if (getrlimit(RLIMIT_NPROC, &rlim) == 0)
    {
        log_message(LOG_INFO, "시스템 프로세스 한계: soft=%ld, hard=%ld", 
                   (long)rlim.rlim_cur, (long)rlim.rlim_max);
        
        int safe_limit = (int)(rlim.rlim_cur * 0.8);
        if (safe_limit < MAX_WORKERS)
        {
            system_max_workers = safe_limit;
            log_message(LOG_WARNING, "MAX_WORKERS를 %d로 제한 (ulimit 기반)", 
                       system_max_workers);
        }
    }
    else
    {
        log_message(LOG_WARNING, "getrlimit(RLIMIT_NPROC) 실패: %s", strerror(errno));
    }
    
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        log_message(LOG_INFO, "파일 디스크립터 한계: soft=%ld, hard=%ld",
                   (long)rlim.rlim_cur, (long)rlim.rlim_max);
    }
}

// 503 응답
static void
send_503_response(int clnt_sock)
{
    const char *response = 
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Server is at capacity. Please try again later.\n";
    
    send(clnt_sock, response, strlen(response), 0);
}

// fork-exec worker 생성
static int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr)
{
    pid_t pid;
    char fd_str[32];
    char session_str[32];
    char ip_str[INET_ADDRSTRLEN];
    char port_str[32];

    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s (session #%d)", 
                   strerror(errno), session_id);
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    pid = fork();
    
    if (pid == -1)
    {
        fork_errors++;
        if (errno == EAGAIN)
        {
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계 (EAGAIN)");
        }
        else if (errno == ENOMEM)
        {
            log_message(LOG_ERROR, "fork() 실패: 메모리 부족 (ENOMEM)");
        }
        else
        {
            log_message(LOG_ERROR, "fork() 실패: %s (errno: %d)", 
                       strerror(errno), errno);
        }
        
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        return -1;
    }
    
    if (pid == 0)
    {
        setup_child_signal_handlers();
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        close(serv_sock);
        
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_GETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        flags &= ~FD_CLOEXEC;
        if (fcntl(clnt_sock, F_SETFD, flags) == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_SETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);

        char *const argv[] = {
            "./worker",
            fd_str,
            session_str,
            ip_str,
            port_str,
            NULL
        };
        execvp("./worker", argv);

        fprintf(stderr, "[자식 #%d] exec() 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
        _exit(127);
    }
    
    total_forks++;
    
    if (worker_count < MAX_WORKERS)
    {
        worker_pids[worker_count++] = pid;
    }
    else
    {
        log_message(LOG_ERROR, "FATAL: Worker PID 배열 초과 (MAX_WORKERS=%d)", MAX_WORKERS);
        kill(pid, SIGKILL);
    }

    close(clnt_sock);
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
    log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", 
               pid, session_id);
    
    return 0;
}

void 
run_server(void)
{
    int serv_sock, clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    int session_id = 0;
    struct pollfd fds[1];
    int poll_result;
    time_t last_cleanup = time(NULL);
    
    start_time = time(NULL);
    g_parent_pid = getpid();
    
    // ===== ✅ SIGPIPE 무시 (필수!) =====
    signal(SIGPIPE, SIG_IGN);
    log_message(LOG_INFO, "SIGPIPE 무시 설정 완료");
    
    setup_parent_signal_handlers();
    log_init();
    
    check_system_limits();
    log_message(LOG_INFO, "Worker 최대 허용: %d개", system_max_workers);

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler; 
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    
    if (sigaction(SIGINT, &shutdown_act, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGINT) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGTERM, &shutdown_act, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGTERM) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    
    log_message(LOG_INFO, "=== Multi-Process Echo Server (fork-exec) 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        exit(1);
    }
    
    log_message(LOG_INFO, "클라이언트 연결 대기 중");
    
    fds[0].fd = serv_sock;
    fds[0].events = POLLIN;

    // 메인 루프
    while (server_running)
    {
        time_t now = time(NULL);
        if (now - last_cleanup >= 30)
        {
            cleanup_dead_workers();
            last_cleanup = now;
            log_message(LOG_DEBUG, "Worker 정리 완료 (현재: %d개, 좀비 회수: %d개)",
                       worker_count, zombie_reaped);
        }
        
        poll_result = poll(fds, 1, 1000);
        
        if (poll_result == -1)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                continue;
            }
            log_message(LOG_ERROR, "poll() 실패: %s (errno: %d)", 
                       strerror(errno), errno);
            break;
        }
        else if (poll_result == 0)
        {
            continue;
        }
        
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            log_message(LOG_ERROR, "poll 에러 이벤트: 0x%x", fds[0].revents);
            
            if (fds[0].revents & POLLNVAL)
            {
                log_message(LOG_ERROR, "POLLNVAL: 서버 종료");
                break;
            }
            continue;
        }
        
        if (fds[0].revents & POLLIN)
        {
            addr_size = sizeof(clnt_addr);
            clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
            
            if (clnt_sock == -1)
            {
                if (!server_running)
                {
                    log_message(LOG_INFO, "종료 시그널로 인한 accept() 중단");
                    break;
                }
                
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                
                log_message(LOG_ERROR, "accept() 실패: %s (errno: %d)", 
                           strerror(errno), errno);
                continue;
            }
            
            if (!server_running)
            {
                log_message(LOG_INFO, "종료 중이므로 새 연결 거부");
                close(clnt_sock);
                break;
            }
            
            session_id++;
            
            if (worker_count >= system_max_workers)
            {
                log_message(LOG_WARNING, "Worker 한계 도달 (%d/%d) - 503 응답",
                           worker_count, system_max_workers);
                
                send_503_response(clnt_sock);
                close(clnt_sock);
                rejected_connections++;
                cleanup_dead_workers();
                continue;
            }
            
            log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d, Workers: %d/%d)",
                       inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), 
                       session_id, worker_count, system_max_workers);
            
            int result = fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr);
            
            if (result == -1)
            {
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
    }
    
    // ===== 종료 처리 =====
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "거부된 연결: %d개 (503)", rejected_connections);
    log_message(LOG_INFO, "회수한 좀비 (운영 중): %d개", zombie_reaped);
    
    cleanup_dead_workers();
    
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    int initial_count = worker_count;
    pid_t worker_pids_copy[MAX_WORKERS];
    memcpy(worker_pids_copy, worker_pids, sizeof(pid_t) * initial_count);
    
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
    log_message(LOG_INFO, "활성 Worker %d개에게 SIGTERM 전송", initial_count);
    
    for (int i = 0; i < initial_count; i++)
    {
        if (kill(worker_pids_copy[i], 0) == 0)
        {
            if (kill(worker_pids_copy[i], SIGTERM) == 0)
            {
                log_message(LOG_DEBUG, "SIGTERM 전송: PID %d", worker_pids_copy[i]);
            }
        }
    }
    
    log_message(LOG_INFO, "Worker 정상 종료 대기 중 (5초)...");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5)
    {
        cleanup_dead_workers();
        
        sigprocmask(SIG_BLOCK, &block_set, &old_set);
        int current_count = worker_count;
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        
        if (current_count == 0)
        {
            log_message(LOG_INFO, "모든 Worker 정상 종료 완료");
            break;
        }
        usleep(100000);
    }
    
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    int remaining_count = worker_count;
    pid_t remaining_pids[MAX_WORKERS];
    memcpy(remaining_pids, worker_pids, sizeof(pid_t) * remaining_count);
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
    int graceful_exits = initial_count - remaining_count;
    log_message(LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개",
               graceful_exits, remaining_count);
    
    int actually_alive = 0;
    for (int i = 0; i < remaining_count; i++)
    {
        if (kill(remaining_pids[i], 0) == 0)
        {
            actually_alive++;
        }
    }
    
    if (actually_alive > 0)
    {
        log_message(LOG_WARNING, "실제 살아있는 Worker: %d개 (강제 종료 수행)", actually_alive);
        
        for (int i = 0; i < remaining_count; i++)
        {
            if (kill(remaining_pids[i], 0) == 0)
            {
                log_message(LOG_WARNING, "SIGKILL 전송: PID %d", remaining_pids[i]);
                kill(remaining_pids[i], SIGKILL);
            }
        }
        
        log_message(LOG_INFO, "SIGKILL 후 커널 처리 대기 (200ms)...");
        usleep(200000);
    }
    else
    {
        log_message(LOG_INFO, "모든 Worker 이미 종료됨 (SIGKILL 불필요)");
    }
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    log_message(LOG_INFO, "SIGCHLD 핸들러 비활성화 후 최종 회수 시작");
    signal(SIGCHLD, SIG_DFL);
    
    int final_count = 0;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        final_count++;
        log_message(LOG_DEBUG, "최종 회수 (WNOHANG): PID %d", pid);
    }
    
    pid = waitpid(-1, &status, 0);
    if (pid > 0)
    {
        final_count++;
        log_message(LOG_DEBUG, "최종 회수 (블로킹): PID %d", pid);
        
        while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        {
            final_count++;
            log_message(LOG_DEBUG, "추가 회수: PID %d", pid);
        }
    }
    else if (pid == -1)
    {
        if (errno == ECHILD)
        {
            log_message(LOG_INFO, "모든 자식 프로세스 회수 완료 (ECHILD)");
        }
        else
        {
            log_message(LOG_ERROR, "waitpid() 에러: %s", strerror(errno));
        }
    }
    
    log_message(LOG_INFO, "====================================");
    log_message(LOG_INFO, "최종 좀비 회수 통계:");
    log_message(LOG_INFO, "  - 운영 중 회수 (SIGCHLD): %d개", zombie_reaped);
    log_message(LOG_INFO, "  - 종료 시 회수 (waitpid): %d개", final_count);
    log_message(LOG_INFO, "  - 총 회수: %d개", zombie_reaped + final_count);
    log_message(LOG_INFO, "  - 생성된 Worker: %d개", total_forks);
    
    if (zombie_reaped + final_count == total_forks)
    {
        log_message(LOG_INFO, "✓ 모든 Worker 정상 회수 확인");
    }
    else
    {
        log_message(LOG_WARNING, "⚠ 회수 불일치: 생성 %d, 회수 %d", 
                   total_forks, zombie_reaped + final_count);
    }
    
    log_message(LOG_INFO, "====================================");
    
    print_resource_limits();
    
    log_message(LOG_INFO, "서버 정상 종료 완료");
}