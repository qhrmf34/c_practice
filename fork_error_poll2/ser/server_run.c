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

// 전역 변수
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int fork_errors = 0;
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
                safe_write("[SIGCHLD] 자식 프로세스 크래시 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 크래시 코드: ");
                safe_write_int(exit_code);
                safe_write(") - 좀비 회수: ");
                safe_write_int(zombie_reaped);
                safe_write("\n");
            }
            else
            {
                safe_write("[SIGCHLD] 자식 프로세스 정상 종료 (PID: ");
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
            safe_write("[SIGCHLD] 자식 프로세스 시그널 종료 (PID: ");
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

void 
run_server(void)
{
    int serv_sock, clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    pid_t pid;
    int session_id = 0;
    struct pollfd fds[1];
    int poll_result;
    
    start_time = time(NULL);
    g_parent_pid = getpid();
    
    //  부모용 Stack trace 설정
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

    log_message(LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        exit(1);
    }

    log_message(LOG_INFO, "클라이언트 연결 대기 중 (poll 사용)");

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
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
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
                // 현재 poll 파일디스크립터로 등록된건 서버소켓, 서버소켓이 유효하지 않으므로 서버 종료
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
            
            //  시그널 블로킹 (critical section) - fork중간에 ctrl+c 시그널 올 경우 부모 자식중 하나만 종료 처리 될 수 있음
            sigset_t block_set, old_set;
            sigemptyset(&block_set);
            sigaddset(&block_set, SIGINT);
            sigaddset(&block_set, SIGTERM);
            sigprocmask(SIG_BLOCK, &block_set, &old_set);
            
            // fork
            pid = fork();

            if (pid == -1)
            {
                //  fork 실패 시그널 블로킹 상태이기에 프로세스, 메모리만 체크
                fork_errors++;
                if (errno == EAGAIN)
                {
                    log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계 (EAGAIN)");
                    log_message(LOG_ERROR, "성공한 fork: %d개, 실패한 fork: %d개",
                               total_forks, fork_errors);
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
                close(clnt_sock);
                
                // 시그널 언블록
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                continue;
            }
            
            //  자식 프로세스 
            if (pid == 0)
            {
                //  자식용 crash handler 재설정
                setup_child_signal_handlers();

                // SIGINT, SIGTERM 무시
                signal(SIGINT, SIG_IGN);
                signal(SIGTERM, SIG_IGN);
                
                // 시그널 마스크 복원
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                
                //  서버 소켓 닫기
                close(serv_sock);
                
                //  클라이언트 처리
                child_process_main(clnt_sock, session_id, clnt_addr);
                
                //  정상 종료
                _exit(0);
            }
            // 부모 프로세스
            total_forks++;
            close(clnt_sock);
            
            // 시그널 언블록
            sigprocmask(SIG_SETMASK, &old_set, NULL);
            
            log_message(LOG_INFO, "자식 프로세스 생성 (PID: %d, Session #%d)", 
                       pid, session_id);
            log_message(LOG_DEBUG, "생성된 자식: %d개, 회수된 좀비: %d개", 
                       total_forks, zombie_reaped);
            
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
    
    int close_result;
    do {
        close_result = close(serv_sock);
    } while (close_result == -1 && errno == EINTR);
    
    if (close_result == -1) {
        log_message(LOG_ERROR, "close() 실패: %s (무시)", strerror(errno));
        // Linux에서는 이미 닫혔으므로 문제 없음
    }
    
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    // 남은 좀비 회수
    log_message(LOG_INFO, "남은 자식 프로세스 대기 중...");
    int final_count = 0;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        final_count++;
    }
    zombie_reaped += final_count;
    log_message(LOG_INFO, "최종 회수된 좀비: %d개 (방금: %d개)", zombie_reaped, final_count);
}