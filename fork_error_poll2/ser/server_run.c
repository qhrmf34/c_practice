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

// 서버 실행 플래그
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int fork_errors = 0;
static int zombie_reaped = 0;
static time_t start_time;

// SIGCHLD 핸들러 - 좀비 프로세스 회수
static void
sigchld_handler(int signo)
{
    int saved_errno = errno;
    pid_t pid;
    int status;
    
    // 모든 종료된 자식 프로세스 회수 
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        zombie_reaped++;
        
        if (WIFEXITED(status))
        {
            printf("[부모] 자식 프로세스 종료 (PID: %d, 종료코드: %d) - 좀비 회수: %d\n",
                   pid, WEXITSTATUS(status), zombie_reaped);
        }
        else if (WIFSIGNALED(status))
        {
            printf("[부모] 자식 프로세스 시그널 종료 (PID: %d, 시그널: %d)\n",
                   pid, WTERMSIG(status));
        }
    }
    
    errno = saved_errno;
}

// 서버 종료 signal handler
static void
shutdown_handler(int signo)
{
    server_running = 0;
    printf("\n[서버] Shutdown signal received...\n");
    time_t end_time = time(NULL);
    printf("[통계] 총 실행 시간: %ld초\n", end_time - start_time);
    printf("[통계] 성공한 fork: %d개\n", total_forks);
    printf("[통계] 실패한 fork: %d개\n", fork_errors);
    printf("[통계] 회수한 좀비: %d개\n", zombie_reaped);
}

// 서버 실행 (메인 루프 - poll 기반)
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
    
    // // SIGCHLD 핸들러 설정 - 좀비 프로세스 회수
    // struct sigaction sa_chld;
    // sa_chld.sa_handler = sigchld_handler;
    // sigemptyset(&sa_chld.sa_mask);
    // sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    // if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
    // {
    //     fprintf(stderr, "[에러] sigaction(SIGCHLD) 설정 실패: %s\n", strerror(errno));
    //     exit(1);
    // }
    
    // SIGINT, SIGTERM 시그널 처리 (Graceful Shutdown)
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler;
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    if (sigaction(SIGINT, &shutdown_act, NULL) == -1)
    {
        fprintf(stderr, "[에러] sigaction(SIGINT) 설정 실패: %s\n", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGTERM, &shutdown_act, NULL) == -1)
    {
        fprintf(stderr, "[에러] sigaction(SIGTERM) 설정 실패: %s\n", strerror(errno));
        exit(1);
    }

    printf("=== Multi-Process Echo Server (Poll + Blocking) ===\n");
    printf("Port: %d\n", PORT);
    printf("시작 시간: %s", ctime(&start_time));
    
    // 서버 소켓 생성 (blocking)
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        fprintf(stderr, "[에러] 서버 소켓 생성 실패\n");
        exit(1);
    }

    printf("[서버] 클라이언트 연결 대기 중 (poll 사용)...\n\n");

    // poll 설정
    fds[0].fd = serv_sock;
    fds[0].events = POLLIN;  // 읽기 가능 이벤트

    // 메인 루프: poll로 accept 제어
    while (server_running)
    {
        // poll로 신규 연결 대기 (1초 timeout)
        poll_result = poll(fds, 1, 1000);
        
        if (poll_result == -1)
        {
            if (errno == EINTR)
            {
                // 시그널에 의한 중단 (정상)
                continue;
            }
            fprintf(stderr, "[에러] poll() 실패: %s\n", strerror(errno));
            break;
        }
        
        if (poll_result == 0)
        {
            // timeout - 신규 연결 없음
            continue;
        }
        
        // POLLIN 이벤트 확인 - 신규 연결 있음
        if (fds[0].revents & POLLIN)
        {
            addr_size = sizeof(clnt_addr);
            clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
            
            if (clnt_sock == -1)
            {
                if (!server_running)
                {
                    printf("[서버] 종료 시그널로 인한 accept() 중단\n");
                    break;
                }
                
                if (errno == EINTR)
                {
                    printf("[서버] accept() interrupted (EINTR), 재시도\n");
                    continue;
                }
                else if (errno == EMFILE)
                {
                    fprintf(stderr, "[에러] accept() 실패: 프로세스 파일 디스크립터 한계 (EMFILE)\n");
                }
                else if (errno == ENFILE)
                {
                    fprintf(stderr, "[에러] accept() 실패: 시스템 파일 테이블 가득 참 (ENFILE)\n");
                }
                else
                {
                    fprintf(stderr, "[에러] accept() 실패: %s (errno: %d)\n", 
                            strerror(errno), errno);
                }
                continue;
            }

            session_id++;
            printf("[서버] 새 연결 수락: %s:%d (Session #%d, fd: %d)\n",
                   inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), 
                   session_id, clnt_sock);

            // fork: 자식 프로세스 생성
            pid = fork();
            
            if (pid == -1)
            {
                // fork 실패
                fork_errors++;
                time_t current_time = time(NULL);
                
                if (errno == EAGAIN)
                {
                    fprintf(stderr, "\n[치명적] fork() 실패: 프로세스 리소스 한계 (EAGAIN)\n");
                    fprintf(stderr, "[통계] 실행 시간: %ld초\n", current_time - start_time);
                    fprintf(stderr, "[통계] 성공한 fork: %d개\n", total_forks);
                    fprintf(stderr, "[통계] 실패한 fork: %d개\n\n", fork_errors);
                }
                else if (errno == ENOMEM)
                {
                    fprintf(stderr, "\n[치명적] fork() 실패: 메모리 부족 (ENOMEM)\n");
                }
                else
                {
                    fprintf(stderr, "\n[에러] fork() 실패: %s (errno: %d)\n", 
                            strerror(errno), errno);
                }
                
                close(clnt_sock);
                continue;
            }
            
            if (pid == 0)
            {
                // 자식 프로세스
                close(serv_sock);  // 자식은 서버 소켓 필요 없음
                
                // 자식 프로세스 메인 함수 실행
                child_process_main(clnt_sock, session_id, clnt_addr);
                
                // 정상 종료
                exit(0);
            }
            else
            {
                // 부모 프로세스
                total_forks++;
                close(clnt_sock);  // 부모는 클라이언트 소켓 필요 없음
                
                printf("[서버] 자식 프로세스 생성 성공 (PID: %d, Session #%d)\n", 
                       pid, session_id);
                printf("[통계] 현재까지 생성된 자식: %d개, 회수된 좀비: %d개\n\n", 
                       total_forks, zombie_reaped);
            }
        }
        
        // 에러 이벤트 체크
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            fprintf(stderr, "[에러] poll 에러 이벤트 발생: 0x%x\n", fds[0].revents);
            break;
        }
    }
    
    print_resource_limits();
    printf("\n[서버] 정상 종료 중...\n");
    
    if (close(serv_sock) == -1)
    {
        fprintf(stderr, "[에러] close(serv_sock) 실패: %s\n", strerror(errno));
    }
    
    printf("[서버] 서버 소켓 닫기 완료\n");
    
    // 남은 좀비 프로세스 회수
    printf("[서버] 남은 자식 프로세스 대기 중...\n");
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        zombie_reaped++;
    }
    printf("[서버] 최종 회수된 좀비: %d개\n", zombie_reaped);
}