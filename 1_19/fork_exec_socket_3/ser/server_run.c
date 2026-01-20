#define _POSIX_C_SOURCE 200809L
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

// 전역 변수
static volatile sig_atomic_t server_running = 1;
static volatile sig_atomic_t active_workers = 0;  // 현재 실행 중인 worker 수
static volatile sig_atomic_t zombie_reaped = 0;   // signal handler에서 안전하게 증가
static volatile sig_atomic_t exec_failure_detected = 0;  // exec 실패 플래그

// 아래는 메인 루프에서만 접근 (signal handler 접근 없음)
static int total_forks = 0;
static int fork_errors = 0;
static int exec_errors = 0;       // SIGCHLD의 플래그를 보고 메인에서 증가
static int worker_limit_hits = 0;
static time_t start_time;
static pid_t g_parent_pid = 0;

// === async-signal-safe 헬퍼 함수들 ===

// 안전한 문자열 출력 (async-signal-safe)
static void 
safe_write(const char *msg)
{
    size_t len = 0;
    while (msg[len]) len++;
    write(STDERR_FILENO, msg, len);
}

// 정수를 문자열로 변환 후 출력 (async-signal-safe)
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
    
    // 역순으로 출력
    while (i > 0)
    {
        write(STDERR_FILENO, &buf[--i], 1);
    }
}

// SIGCHLD 핸들러 - 좀비 프로세스 회수 (async-signal-safe)
static void 
sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        zombie_reaped++;      // sig_atomic_t라서 안전
        active_workers--;     // sig_atomic_t라서 안전
        
        // write()를 사용한 안전한 로깅 (async-signal-safe)
        if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            
            // exec 실패 감지 (exit code 127)
            if (exit_code == 127)
            {
                exec_failure_detected = 1;  // 플래그만 설정 (sig_atomic_t)
                safe_write("[SIGCHLD] Worker exec 실패 (PID: ");
                safe_write_int(pid);
                safe_write(", exit_code: 127)\n");
            }
            // 크래시로 인한 종료 (128 + signal number)
            else if (exit_code >= 128)
            {
                safe_write("[SIGCHLD] Worker 크래시 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 크래시 코드: ");
                safe_write_int(exit_code);
                safe_write(")\n");
            }
            // 일반 에러 종료
            else if (exit_code != 0)
            {
                safe_write("[SIGCHLD] Worker 에러 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 종료코드: ");
                safe_write_int(exit_code);
                safe_write(")\n");
            }
            // 정상 종료 (exit_code == 0)
            else
            {
                safe_write("[SIGCHLD] Worker 정상 종료 (PID: ");
                safe_write_int(pid);
                safe_write(")\n");
            }
        }
        else if (WIFSIGNALED(status))
        {
            // 시그널로 종료 (crash)
            int sig = WTERMSIG(status);
            safe_write("[SIGCHLD] Worker 시그널 종료 (PID: ");
            safe_write_int(pid);
            safe_write(", 시그널: ");
            safe_write_int(sig);
            safe_write(")\n");
        }
        
        safe_write("[SIGCHLD] 좀비 회수: ");
        safe_write_int(zombie_reaped);
        safe_write(", 활성 worker: ");
        safe_write_int(active_workers);
        safe_write("\n");
    }
    
    errno = saved_errno;
}

// 서버 종료 signal handler (async-signal-safe)
static void 
shutdown_handler(int signo)
{
    (void)signo;
    
    // 부모 프로세스만 처리
    if (getpid() != g_parent_pid)
    {
        _exit(0);  // 자식은 조용히 종료
    }
    
    // 플래그만 설정 (async-signal-safe)
    server_running = 0;
    
    safe_write("\n[SHUTDOWN] 서버 종료 시그널 수신, 모든 worker에 SIGTERM 전송\n");
    
    // 모든 worker에게 SIGTERM 전송 (프로세스 그룹 전체)
    kill(0, SIGTERM);
}

/*
 * fork-exec 방식으로 worker 프로세스 생성
 * 
 * 파이프를 통한 exec 동기화:
 * - exec 성공 시: 파이프 자동 닫힘 (FD_CLOEXEC) → 부모는 EOF 읽음
 * - exec 실패 시: 자식이 errno 전송 → 부모가 읽고 에러 처리
 */
static int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr)
{
    pid_t pid;
    char fd_str[32];
    char session_str[32];
    char ip_str[INET_ADDRSTRLEN];
    char port_str[32];
    int exec_pipe[2];  // exec 동기화용 파이프
    
    // === worker 수 제한 체크 ===
    if (active_workers >= MAX_WORKERS)
    {
        worker_limit_hits++;
        log_message(LOG_WARNING, "Worker 제한 도달 (%d/%d), 연결 거부 (Session #%d)", 
                   active_workers, MAX_WORKERS, session_id);
        return -1;
    }
    
    // === exec 동기화용 파이프 생성 ===
    if (pipe(exec_pipe) == -1)
    {
        log_message(LOG_ERROR, "pipe() 실패: %s", strerror(errno));
        return -1;
    }
    
    // === exec 인자 준비 ===
    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s (session #%d)", 
                   strerror(errno), session_id);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return -1;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    // === 시그널 블로킹 (critical section) ===
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    // === fork ===
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
        
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        return -1;
    }
    
    // === 자식 프로세스 ===
    if (pid == 0)
    {
        // 시그널 마스크 복원
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        
        // read end 닫기 (자식은 write만 사용)
        close(exec_pipe[0]);
        
        // write end를 FD_CLOEXEC로 설정 (exec 성공 시 자동 닫힘)
        int flags = fcntl(exec_pipe[1], F_GETFD);
        if (flags != -1)
        {
            fcntl(exec_pipe[1], F_SETFD, flags | FD_CLOEXEC);
        }
        
        // 서버 소켓 닫기
        close(serv_sock);
        
        // 클라이언트 소켓 FD_CLOEXEC 제거
        flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1)
        {
            int err = errno;
            write(exec_pipe[1], &err, sizeof(err));  // 에러 전송
            _exit(EXIT_FAILURE);
        }
        
        flags &= ~FD_CLOEXEC;
        if (fcntl(clnt_sock, F_SETFD, flags) == -1)
        {
            int err = errno;
            write(exec_pipe[1], &err, sizeof(err));
            _exit(EXIT_FAILURE);
        }
        
        // 시그널 핸들러 리셋
        struct sigaction sa_default;
        sa_default.sa_handler = SIG_DFL;
        sigemptyset(&sa_default.sa_mask);
        sa_default.sa_flags = 0;
        
        sigaction(SIGINT, &sa_default, NULL);
        sigaction(SIGTERM, &sa_default, NULL);
        sigaction(SIGCHLD, &sa_default, NULL);
        sigaction(SIGPIPE, &sa_default, NULL);
        
        // === exec 수행 ===
        char *const argv[] = {
            "./worker",
            fd_str,
            session_str,
            ip_str,
            port_str,
            NULL
        };
        
        execvp("./worker", argv);
        
        // === exec 실패 ===
        int exec_errno = errno;
        write(exec_pipe[1], &exec_errno, sizeof(exec_errno));  // 에러 코드 전송
        
        fprintf(stderr, "[자식 #%d] exec() 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
        
        if (errno == ENOENT)
        {
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음\n", session_id);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음\n", session_id);
        }
        
        _exit(127);
    }
    
    // === 부모 프로세스 ===
    
    // write end 닫기 (부모는 read만 사용)
    close(exec_pipe[1]);
    
    // fork 성공 카운트 (exec 성공 여부는 아직 모름)
    total_forks++;
    
    // === exec 성공 여부 확인 (파이프로 동기화) ===
    int exec_errno = 0;
    struct pollfd pfd;
    pfd.fd = exec_pipe[0];
    pfd.events = POLLIN;
    
    // 100ms timeout (exec는 빠르게 성공/실패함)
    int poll_ret = poll(&pfd, 1, 100);
    
    if (poll_ret > 0)
    {
        // 파이프에서 데이터 읽기
        ssize_t n = read(exec_pipe[0], &exec_errno, sizeof(exec_errno));
        
        if (n > 0)
        {
            // 자식이 에러 코드를 전송 → exec 실패
            exec_errors++;
            log_message(LOG_ERROR, "Worker exec 실패 (Session #%d, errno: %s)", 
                       session_id, strerror(exec_errno));
            close(exec_pipe[0]);
            close(clnt_sock);
            sigprocmask(SIG_SETMASK, &old_set, NULL);
            return -1;
        }
        else if (n == 0)
        {
            // EOF → exec 성공 (파이프가 FD_CLOEXEC로 닫힘)
            active_workers++;
            log_message(LOG_INFO, "Worker 프로세스 생성 및 exec 성공 (PID: %d, Session #%d, 활성: %d/%d)", 
                       pid, session_id, active_workers, MAX_WORKERS);
        }
    }
    else if (poll_ret == 0)
    {
        // Timeout → 이상하지만 일단 성공으로 간주
        active_workers++;
        log_message(LOG_WARNING, "Worker exec 확인 timeout (PID: %d, Session #%d), 성공으로 가정", 
                   pid, session_id);
    }
    else
    {
        // poll 에러
        if (errno == EINTR)
        {
            // 시그널 인터럽트 → 성공으로 간주
            active_workers++;
            log_message(LOG_INFO, "Worker exec 확인 중 시그널 인터럽트 (PID: %d, Session #%d)", 
                       pid, session_id);
        }
        else
        {
            log_message(LOG_ERROR, "Worker exec 확인 poll 실패: %s", strerror(errno));
        }
    }
    
    close(exec_pipe[0]);
    close(clnt_sock);  // 부모는 더 이상 사용하지 않음
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
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
    
    start_time = time(NULL);
    g_parent_pid = getpid();
    
    // 부모용 Stack trace 설정
    setup_parent_signal_handlers();
    
    log_init();

    // === SIGPIPE 무시 (중요!) ===
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGPIPE) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    log_message(LOG_INFO, "SIGPIPE 무시 설정 완료");

    // === SIGCHLD 핸들러 설정 ===
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    
    // === SIGINT, SIGTERM 핸들러 ===
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler; 
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    
    if (sigaction(SIGINT, &shutdown_act, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGINT) 설정 실패: %s", strerror(errno));
        exit(1);
    }

    log_message(LOG_INFO, "=== Multi-Process Echo Server (fork-exec) 시작 ===");
    log_message(LOG_INFO, "Port: %d, 최대 Worker: %d", PORT, MAX_WORKERS);
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        exit(1);
    }

    log_message(LOG_INFO, "클라이언트 연결 대기 중 (poll 사용, fork-exec 방식)");

    // poll 설정
    fds[0].fd = serv_sock;
    fds[0].events = POLLIN;

    // 메인 루프
    while (server_running)
    {
        // === exec 실패 플래그 체크 (signal handler에서 설정됨) ===
        if (exec_failure_detected)
        {
            exec_errors++;
            exec_failure_detected = 0;  // 플래그 리셋
            log_message(LOG_ERROR, "exec 실패 감지, 총 exec 실패: %d", exec_errors);
        }
        
        // poll로 신규 연결 대기 (1초 timeout)
        poll_result = poll(fds, 1, 1000);
        
        if (poll_result == -1)
        {
            if (errno == EINTR)
            {
                // 시그널 인터럽트 (정상) - 재시도
                continue;
            }
            
            // 심각한 에러 - 종료
            log_message(LOG_ERROR, "poll() 실패: %s (errno: %d)", 
                       strerror(errno), errno);
            break;
        }
        else if (poll_result == 0)
        {
            // 타임아웃 (신규 연결 없음) - 재시도
            continue;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            log_message(LOG_ERROR, "poll 에러 이벤트 발생: 0x%x", fds[0].revents);
            
            if (fds[0].revents & POLLERR)
            {
                log_message(LOG_ERROR, "POLLERR: 소켓 에러");
                continue;
            }
            if (fds[0].revents & POLLHUP)
            {
                log_message(LOG_ERROR, "POLLHUP: 연결 끊김");
                continue;
            }
            if (fds[0].revents & POLLNVAL)
            {
                log_message(LOG_ERROR, "POLLNVAL: 잘못된 FD");
                break;
            }
        }
        
        // POLLIN 이벤트 확인
        if (fds[0].revents & POLLIN)
        {
            addr_size = sizeof(clnt_addr);
            clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
            
            if (clnt_sock == -1)
            {
                // 서버 종료 중
                if (!server_running)
                {
                    log_message(LOG_INFO, "종료 시그널로 인한 accept() 중단");
                    break;
                }
                
                // EINTR - 시그널 인터럽트 (재시도)
                if (errno == EINTR)
                {
                    log_message(LOG_DEBUG, "accept() interrupted (EINTR), 재시도");
                    continue;
                }
                
                // EAGAIN/EWOULDBLOCK 
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    log_message(LOG_DEBUG, "accept() 일시적으로 불가 (EAGAIN), 재시도");
                    continue;
                }
                
                // 기타 심각한 에러
                log_message(LOG_ERROR, "accept() 실패: %s (errno: %d)", 
                           strerror(errno), errno);
                continue;
            }
            
            // 연결 수락 성공
            session_id++;
            log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d, fd: %d)",
                       inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), 
                       session_id, clnt_sock);
            
            // === fork-exec로 worker 생성 ===
            int result = fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr);
            
            if (result == -1)
            {
                // fork 실패 또는 worker 제한 - 클라이언트 소켓을 직접 닫아야 함
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
            // fork 성공 시 clnt_sock은 이미 fork_and_exec_worker에서 닫힘
        }
    }
    
    // === 종료 처리 ===
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "실패한 exec: %d개", exec_errors);
    log_message(LOG_INFO, "worker 제한 거부: %d개", worker_limit_hits);
    log_message(LOG_INFO, "회수한 좀비: %d개", (int)zombie_reaped);
    log_message(LOG_INFO, "현재 활성 worker: %d개", (int)active_workers);
    
    print_resource_limits();
    log_message(LOG_INFO, "서버 정상 종료 중...");
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    // 남은 worker 강제 종료 (5초 대기)
    log_message(LOG_INFO, "남은 Worker 프로세스 대기 중 (최대 5초)...");
    time_t shutdown_start = time(NULL);
    int final_count = 0;
    
    while (active_workers > 0 && (time(NULL) - shutdown_start) < 5)
    {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid > 0)
        {
            final_count++;
        }
        else if (pid == 0)
        {
            // 아직 종료되지 않은 프로세스 있음 - 짧게 대기
            struct timespec ts = {0, 100000000};  // 100ms
            nanosleep(&ts, NULL);
        }
        else
        {
            // 에러 또는 더 이상 회수할 프로세스 없음
            break;
        }
    }
    
    // 5초 후에도 남아있으면 강제 종료
    if (active_workers > 0)
    {
        log_message(LOG_WARNING, "%d개 worker가 여전히 실행 중, SIGKILL 전송", (int)active_workers);
        kill(0, SIGKILL);
        sleep(1);
        
        // 최종 회수
        while (waitpid(-1, NULL, WNOHANG) > 0)
        {
            final_count++;
        }
    }
    
    log_message(LOG_INFO, "shutdown 중 회수된 좀비: %d개", final_count);
    log_message(LOG_INFO, "총 회수된 좀비: %d개 (SIGCHLD: %d개 + shutdown: %d개)", 
               (int)zombie_reaped + final_count, (int)zombie_reaped, final_count);
    log_message(LOG_INFO, "서버 종료 완료");
}