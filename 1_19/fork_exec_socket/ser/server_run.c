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

static void 
safe_write(const char *msg)
{
    size_t len = 0;
    while (msg[len]) len++;
    (void)write(STDERR_FILENO, msg, len);  // 명시적으로 반환값 무시
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
    
    // 역순으로 출력
    while (i > 0)
    {
        (void)write(STDERR_FILENO, &buf[--i], 1);  // 명시적으로 반환값 무시
    }
}

// SIGCHLD 핸들러
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
            
            if (exit_code >= 128)
            {
                safe_write("[SIGCHLD] Worker 프로세스 크래시 종료 (PID: ");
                safe_write_int(pid);
                safe_write(", 크래시 코드: ");
                safe_write_int(exit_code);
                safe_write(") - 좀비 회수: ");
                safe_write_int(zombie_reaped);
                safe_write("\n");
            }
            else
            {
                safe_write("[SIGCHLD] Worker 프로세스 정상 종료 (PID: ");
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
            int sig = WTERMSIG(status);
            safe_write("[SIGCHLD] Worker 프로세스 시그널 종료 (PID: ");
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

// 서버 종료 handler
static void 
shutdown_handler(int signo)
{
    (void)signo;
    
    if (getpid() != g_parent_pid)
    {
        _exit(0);
    }
    
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

    log_message(LOG_INFO, "=== Multi-Process Echo Server 시작 (fork+exec 모드) ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        exit(1);
    }

    log_message(LOG_INFO, "클라이언트 연결 대기 중 (poll 사용, fork+exec 모드)");

    // poll 설정
    fds[0].fd = serv_sock;
    fds[0].events = POLLIN;

    // 메인 루프
    while (server_running)
    {
        poll_result = poll(fds, 1, 1000);
        
        if (poll_result == -1)
        {
            if (errno == EINTR)
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
                if (!server_running)
                {
                    log_message(LOG_INFO, "종료 시그널로 인한 accept() 중단");
                    break;
                }
                
                if (errno == EINTR)
                {
                    log_message(LOG_DEBUG, "accept() interrupted (EINTR), 재시도");
                    continue;
                }
                
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    log_message(LOG_DEBUG, "accept() 일시적으로 불가 (EAGAIN), 재시도");
                    continue;
                }
                
                log_message(LOG_ERROR, "accept() 실패: %s (errno: %d)", 
                           strerror(errno), errno);
                continue;
            }
            
            session_id++;
            log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d, fd: %d)",
                       inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), 
                       session_id, clnt_sock);
            
            // === fork 전에 시그널 블로킹 ===
            sigset_t block_set, old_set;
            sigemptyset(&block_set);
            sigaddset(&block_set, SIGINT);
            sigaddset(&block_set, SIGTERM);
            sigprocmask(SIG_BLOCK, &block_set, &old_set);
            
            // fork
            pid = fork();

            if (pid == -1)
            {
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
                
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                continue;
            }
            
            // 자식 프로세스
            if (pid == 0)
            {
                // 시그널 마스크 복원
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                
                // 서버 소켓 닫기
                close(serv_sock);
                
                // FD와 session_id를 문자열로 변환
                char fd_str[16];
                char session_str[16];
                snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
                snprintf(session_str, sizeof(session_str), "%d", session_id);
                
                // exec로 worker 프로그램 실행
                execl("./worker", "worker", fd_str, session_str, (char*)NULL);
                
                // exec 실패 시에만 도달
                fprintf(stderr, "[자식 #%d] execl() 실패: %s (errno: %d)\n", 
                       session_id, strerror(errno), errno);
                fprintf(stderr, "[자식 #%d] worker 실행 파일을 찾을 수 없습니다. ./worker 경로를 확인하세요.\n", 
                       session_id);
                
                // exec 실패 시 자식 프로세스 종료
                _exit(127);
            }
            else
            {
                // 부모 프로세스
                total_forks++;
                close(clnt_sock);
                
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                
                log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", 
                           pid, session_id);
                log_message(LOG_DEBUG, "생성된 worker: %d개, 회수된 좀비: %d개", 
                           total_forks, zombie_reaped);
            }
        }
    }
    
    // === 종료 처리 ===
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "회수한 좀비: %d개", zombie_reaped);
    
    // 전체 시스템 상태 출력
    printf("\n");
    print_system_status();
    
    print_resource_limits();
    log_message(LOG_INFO, "서버 정상 종료 중...");
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    // 남은 좀비 회수
    log_message(LOG_INFO, "남은 worker 프로세스 대기 중...");
    int final_count = 0;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        final_count++;
    }
    zombie_reaped += final_count;
    log_message(LOG_INFO, "최종 회수된 좀비: %d개 (방금: %d개)", zombie_reaped, final_count);
}