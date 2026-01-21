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
//server_run 수정 전 코드 - > signal에서 동기 에러 발생(log_info오류)
// 전역 변수
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int fork_errors = 0;
static int zombie_reaped = 0;
static time_t start_time;
static pid_t g_parent_pid = 0;

// SIGCHLD 핸들러 - 좀비 프로세스 회수
static void 
sigchld_handler(int signo)
{
    int saved_errno = errno;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        zombie_reaped++;
        if (WIFEXITED(status))
        {
            // 정상 종료
            int exit_code = WEXITSTATUS(status);
            
            // 크래시로 인한 종료인지 확인 (128 + signal number)
            if (exit_code >= 128)
            {
                log_message(LOG_ERROR, "자식 프로세스 크래시 종료 (PID: %d, 크래시 코드: %d) - 좀비 회수: %d",
                           pid, exit_code, zombie_reaped);
            }
            else
            {
                log_message(LOG_INFO, "자식 프로세스 정상 종료 (PID: %d, 종료코드: %d) - 좀비 회수: %d",
                           pid, exit_code, zombie_reaped);
            }
        }
        else if (WIFSIGNALED(status))
        {
            // 시그널로 종료 (crash)
            int sig = WTERMSIG(status);
            log_message(LOG_ERROR, "자식 프로세스 시그널 종료 (PID: %d, 시그널: %d - %s) - 좀비 회수: %d",
                       pid, sig, strsignal(sig), zombie_reaped);
        }
    }
    
    errno = saved_errno;
}

// 서버 종료 signal handler
static void 
shutdown_handler(int signo)
{
    // 부모 프로세스만 처리
    // 현재 시그널 신호를 무시한 자식이 만약 여기로 들어오면
    if (getpid() != g_parent_pid)
    {
        exit(0);  // 자식은 조용히 종료
    }
    server_running = 0;
    time_t end_time = time(NULL);

    log_message(LOG_INFO, "서버 종료 시그널 수신 (시그널: %d)", signo);
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "회수한 좀비: %d개", zombie_reaped);
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
                continue;
            }
            //  자식 프로세스 
            if (pid == 0)
            {
                //  자식용 crash handler 재설정
                setup_child_signal_handlers();

                // SIGPIPE 무시 - 클라이언트가 소켓 연결을 강제로 끊었을 경우 write시 즉시 종료를 막음
                signal(SIGINT, SIG_IGN);
                signal(SIGTERM, SIG_IGN);
                
                //  서버 소켓 닫기
                close(serv_sock);
                
                //  클라이언트 처리
                child_process_main(clnt_sock, session_id, clnt_addr);
                
                //  정상 종료
                _exit(0);
            }
            else
            {
                // 부모 프로세스
                total_forks++;
                close(clnt_sock);
                log_message(LOG_INFO, "자식 프로세스 생성 (PID: %d, Session #%d)", 
                           pid, session_id);
                log_message(LOG_DEBUG, "생성된 자식: %d개, 회수된 좀비: %d개", 
                           total_forks, zombie_reaped);
            }
        }
    }
    // 종료 처리
    print_resource_limits();
    log_message(LOG_INFO, "서버 정상 종료 중...");
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
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