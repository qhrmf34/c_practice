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
static int total_forks = 0;
static int total_execs = 0;
static int fork_errors = 0;
static int exec_errors = 0;
static int zombie_reaped = 0;
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
        zombie_reaped++;
        
        // write()를 사용한 안전한 로깅 (async-signal-safe)
        if (WIFEXITED(status))
        {
            // 정상 종료
            int exit_code = WEXITSTATUS(status);
            
            // 크래시로 인한 종료인지 확인 (128 + signal number)
            if (exit_code >= 128)
            {
                safe_write("[SIGCHLD] Worker 크래시 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 크래시 코드: ");
                safe_write_int(exit_code);
                safe_write(") - 좀비 회수: ");
                safe_write_int(zombie_reaped);
                safe_write("\n");
            }
            else
            {
                safe_write("[SIGCHLD] Worker 에러 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 종료코드: ");
                safe_write_int(exit_code);
                safe_write(") - 좀비 회수: ");
                safe_write_int(zombie_reaped);
                safe_write("\n");
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
            safe_write(") - 좀비 회수: ");
            safe_write_int(zombie_reaped);
            safe_write("\n");
        }
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
    // 로그는 메인 루프 밖에서 출력
    server_running = 0;
}

/*
 * fork-exec 방식으로 worker 프로세스 생성
 * 
 * 자식 프로세스는 exec 성공 시 반환하지 않고 worker 프로그램으로 대체됨
 */
static int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr)
{
    pid_t pid;
    char fd_str[32];
    char session_str[32];
    char ip_str[INET_ADDRSTRLEN];
    char port_str[32];
    
    //exec 인자 준비 
    
    // FD를 문자열로 변환
    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    
    //  session_id를 문자열로 변환
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    
    //IP 주소를 문자열로 변환
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s (session #%d)", 
                   strerror(errno), session_id);
        return -1;
    }
    
    //포트를 문자열로 변환
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    
    //  시그널 블로킹 (critical section) ===
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    //  fork 
    pid = fork();
    
    if (pid == -1)
    {
        //  fork 실패 
        fork_errors++;
        
        if (errno == EAGAIN)
        {
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계 (EAGAIN)");
            log_message(LOG_ERROR, "통계: fork 성공=%d, fork 실패=%d, exec 성공=%d, exec 실패=%d",
                       total_forks, fork_errors, total_execs, exec_errors);
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
        
        // 시그널 언블록
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        return -1;
    }
    
    // 자식 프로세스 - exec 수행 
    if (pid == 0)
    {
        // 시그널 마스크 복원
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        
        // 서버 소켓 닫기 (자식에게 불필요)
        close(serv_sock);  // 전역 변수 serv_sock을 닫음 (run_server에서 정의)
        
        //  FD_CLOEXEC 플래그 제거 (exec 후에도 FD 유지)
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_GETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        flags &= ~FD_CLOEXEC;  // FD_CLOEXEC 제거
        if (fcntl(clnt_sock, F_SETFD, flags) == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_SETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        // SIGINT, SIGTERM 핸들러 리셋 (worker는 이 시그널을 무시)
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        
        //  exec 수행
        char *const argv[] = {
            "./worker",      // worker 프로그램 경로
            fd_str,          // argv[1]: client_sock FD
            session_str,     // argv[2]: session_id
            ip_str,          // argv[3]: client IP
            port_str,        // argv[4]: client port
            NULL
        };
        
        execvp("./worker", argv);
        
        // === exec 실패 시 (여기 도달하면 exec 실패) ===
        fprintf(stderr, "[자식 #%d] exec() 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
        
        // 상세 에러 메시지
        if (errno == ENOENT)
        {
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음 (ENOENT)\n", 
                    session_id);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음 (EACCES)\n", 
                    session_id);
        }
        else if (errno == ENOMEM)
        {
            fprintf(stderr, "[자식 #%d] 메모리 부족 (ENOMEM)\n", 
                    session_id);
        }
        
        // exec 실패 시 특수 exit code 반환 (부모가 감지 가능)
        _exit(127);  // exec 실패 표준 exit code
    }
    
    // 부모 프로세스 - 후처리 ===
    
    // fork 성공 카운트
    total_forks++;
    
    // 클라이언트 소켓 닫기 (부모는 더 이상 사용하지 않음)
    close(clnt_sock);
    
    // 시그널 언블록
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
    // 로그 출력
    log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", 
               pid, session_id);
    log_message(LOG_DEBUG, "fork 성공=%d, exec 대기 중...", total_forks);
    
    // exec 성공 여부는 SIGCHLD에서 확인
    // exit code가 127이면 exec 실패
    
    return 0;  // 부모 프로세스는 성공 반환
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

    // SIGCHLD 핸들러 설정
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    
    // SIGINT, SIGTERM 핸들러
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
                // 서버 소켓이 유효하지 않으므로 서버 종료
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
                // fork 실패 - 클라이언트 소켓을 직접 닫아야 함
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
            else
            {
                // fork 성공 (exec 성공 여부는 SIGCHLD에서 확인)
                // clnt_sock은 이미 fork_and_exec_worker에서 닫힘
            }
        }
    }
    
    // === 종료 처리 (시그널 핸들러 외부에서 로그 출력) ===
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "회수한 좀비: %d개", zombie_reaped);
    
    print_resource_limits();
    log_message(LOG_INFO, "서버 정상 종료 중...");
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    // 남은 좀비 회수
    log_message(LOG_INFO, "남은 Worker 프로세스 대기 중...");
    int final_count = 0;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        final_count++;
    }
    zombie_reaped += final_count;
    log_message(LOG_INFO, "최종 회수된 좀비: %d개 (방금: %d개)", zombie_reaped, final_count);
}