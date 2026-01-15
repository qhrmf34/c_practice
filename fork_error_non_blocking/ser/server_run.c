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
#include <fcntl.h>

// 서버 실행 플래그
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int fork_errors = 0;
static time_t start_time;

// SIGCHLD 핸들러 제거 - 좀비 프로세스가 쌓이도록 함!
// 좀비 프로세스도 프로세스 테이블을 차지하므로 fork() 고갈 발생

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
    printf("\n[참고] 좀비 프로세스 확인: ps aux | grep defunct\n");
}

// 서버 실행 (메인 루프)
void
run_server(void)
{
    int serv_sock, clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    pid_t pid;
    int session_id = 0;
    int sv[2];  // socketpair for parent-child communication
    char parent_buf[BUF_SIZE];
    
    start_time = time(NULL);
    
    // SIGCHLD 핸들러 제거 - 좀비 프로세스가 쌓이도록 함!
    // signal(SIGCHLD, SIG_IGN); 도 하지 않음
    
    // SIGINT, SIGTERM 시그널 처리 (Graceful Shutdown)
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler;
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    if (sigaction(SIGINT, &shutdown_act, 0) == -1)
    {
        fprintf(stderr, "[에러] sigaction(SIGINT) 설정 실패: %s\n", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGTERM, &shutdown_act, 0) == -1)
    {
        fprintf(stderr, "[에러] sigaction(SIGTERM) 설정 실패: %s\n", strerror(errno));
        exit(1);
    }

    printf("=== Multi-Process Echo Server (Fork Test) ===\n");
    printf("Port: %d\n", PORT);
    printf("시작 시간: %s", ctime(&start_time));
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        fprintf(stderr, "[에러] 서버 소켓 생성 실패\n");
        exit(1);
    }

    printf("[서버] 클라이언트 연결 대기 중...\n\n");

    // 메인 루프: accept() 후 fork()
    while (server_running)
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
                fprintf(stderr, "[에러] accept() 실패: 프로세스 파일 디스크립터 한계 도달 (EMFILE)\n");
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

        // socketpair 생성 (부모-자식 통신용)
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
        {
            fprintf(stderr, "[에러] socketpair() 생성 실패: %s (errno: %d)\n", 
                    strerror(errno), errno);
            close(clnt_sock);
            continue;
        }

        // fork: 자식 프로세스 생성
        pid = fork();
        
        if (pid == -1)
        {
            // fork 실패
            fork_errors++;
            time_t current_time = time(NULL);
            
            if (errno == EAGAIN)
            {
                fprintf(stderr, "\n[치명적] fork() 실패: 프로세스 리소스 한계 도달 (EAGAIN)\n");
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
            close(sv[0]);
            close(sv[1]);
            continue;
        }
        
        if (pid == 0)
        {
            // 자식 프로세스 
            close(serv_sock);  // 자식은 서버 소켓 필요 없음
            close(sv[0]);      // 부모쪽 파이프 닫기
            
            // 클라이언트 처리
            handle_client(clnt_sock, session_id, clnt_addr, sv[1]);
            
            // 처리 완료 후 자식 프로세스 종료
            // 부모가 waitpid()를 안 하므로 → 좀비 프로세스가 됨!
            exit(0);
        }
        else
        {
            // 부모 프로세스
            total_forks++;
            close(clnt_sock);  // 부모는 클라이언트 소켓 필요 없음
            close(sv[1]);      // 자식쪽 파이프 닫기
            
            printf("[서버] 자식 프로세스 생성 성공 (PID: %d, Session #%d)\n", 
                   pid, session_id);
            printf("[통계] 현재까지 생성된 자식 프로세스: %d개\n", total_forks);
            
            // 자식으로부터 메시지 읽기 (non-blocking) - 짧은 시간 대기
            int flags = fcntl(sv[0], F_GETFL, 0);
            fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
            
            int read_len = read(sv[0], parent_buf, BUF_SIZE - 1);
            if (read_len > 0)
            {
                parent_buf[read_len] = 0;
                printf("[부모] 자식으로부터 메시지: %s\n", parent_buf);
            }
            else if (read_len == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                fprintf(stderr, "[부모] 자식으로부터 메시지 읽기 실패: %s\n", strerror(errno));
            }
            
            // 이제 파이프 닫기
            close(sv[0]);
            printf("\n");
        }
    }
    
    print_resource_limits();
    printf("\n[서버] 정상 종료 중...\n");
    
    if (close(serv_sock) == -1)
    {
        fprintf(stderr, "[에러] close(serv_sock) 실패: %s\n", strerror(errno));
    }
    
    printf("[서버] 서버 소켓 닫기 완료\n");
}