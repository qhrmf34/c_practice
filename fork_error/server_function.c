#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h>
#include <fcntl.h>

// 서버 실행 플래그
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int fork_errors = 0;
static time_t start_time;

// 좀비 프로세스 방지를 위한 signal handler
static void
sigchld_handler(int signo)
{
    int saved_errno = errno;
    int status;
    pid_t pid;
    
    // 종료된 자식 프로세스 정리
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFEXITED(status))
        {
            printf("[부모] 자식 프로세스 PID %d 정상 종료 (exit code: %d)\n", 
                   pid, WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status))
        {
            printf("[부모] 자식 프로세스 PID %d 시그널로 종료 (signal: %d)\n", 
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
}

// 클라이언트 처리 함수 (자식 프로세스에서 실행)
void
handle_client(int clnt_sock, int session_id, struct sockaddr_in clnt_addr, int parent_pipe)
{
    // SIGPIPE 무시 (부모가 socketpair를 닫아도 프로세스가 죽지 않도록)
    signal(SIGPIPE, SIG_IGN);
    
    char buf[BUF_SIZE];
    char msg_to_parent[BUF_SIZE];
    int str_len;
    int count = 0;
    int write_result;

    printf(" [자식 #%d (PID:%d)] 시작\n", session_id, getpid());

    // 부모에게 시작 알림
    snprintf(msg_to_parent, BUF_SIZE, "CHILD_START:Session#%d,PID:%d", session_id, getpid());
    write_result = write(parent_pipe, msg_to_parent, strlen(msg_to_parent));
    if (write_result == -1)
    {
        fprintf(stderr, " [자식 #%d] 부모에게 메시지 전송 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
    }
    else if (write_result != (int)strlen(msg_to_parent))
    {
        fprintf(stderr, " [자식 #%d] 부모에게 부분 write: %d/%lu bytes\n", 
                session_id, write_result, strlen(msg_to_parent));
    }

    // 클라이언트가 연결을 끊을 때까지 계속 처리
    while (1)
    {
        str_len = read(clnt_sock, buf, BUF_SIZE - 1);
        
        if (str_len <= 0)
        {
            if (str_len == 0)
            {
                printf(" [자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    printf(" [자식 #%d] Non-blocking read (EAGAIN/EWOULDBLOCK), 재시도\n", session_id);
                    continue;
                }
                else if (errno == EINTR)
                {
                    printf(" [자식 #%d] Read interrupted by signal (EINTR), 재시도\n", session_id);
                    continue;
                }
                else if (errno == ECONNRESET)
                {
                    fprintf(stderr, " [자식 #%d] 연결 리셋 (ECONNRESET)\n", session_id);
                }
                else if (errno == ETIMEDOUT)
                {
                    fprintf(stderr, " [자식 #%d] 연결 타임아웃 (ETIMEDOUT)\n", session_id);
                }
                else
                {
                    fprintf(stderr, " [자식 #%d] read() error: %s (errno: %d)\n", 
                            session_id, strerror(errno), errno);
                }
            }
            break;
        }

        buf[str_len] = 0;

        // Echo back
        write_result = write(clnt_sock, buf, str_len);
        if (write_result == -1)
        {
            if (errno == EPIPE)
            {
                fprintf(stderr, "  [자식 #%d] write() error: Broken pipe (EPIPE)\n", session_id);
            }
            else if (errno == ECONNRESET)
            {
                fprintf(stderr, "  [자식 #%d] write() error: Connection reset (ECONNRESET)\n", session_id);
            }
            else if (errno == EINTR)
            {
                fprintf(stderr, "  [자식 #%d] write() interrupted (EINTR), 재시도 필요\n", session_id);
                continue;
            }
            else
            {
                fprintf(stderr, "  [자식 #%d] write() error: %s (errno: %d)\n", 
                        session_id, strerror(errno), errno);
            }
            break;
        }
        else if (write_result != str_len)
        {
            fprintf(stderr, "  [자식 #%d] 부분 write: %d/%d bytes\n", 
                    session_id, write_result, str_len);
        }
        
        count++;
    }

    printf("  [자식 #%d (PID:%d)] %d I/O 완료, 종료\n", 
           session_id, getpid(), count);
    
    // 부모에게 종료 알림
    snprintf(msg_to_parent, BUF_SIZE, "CHILD_END:Session#%d,IO_Count:%d", session_id, count);
    write_result = write(parent_pipe, msg_to_parent, strlen(msg_to_parent));
    if (write_result == -1)
    {
        fprintf(stderr, " [자식 #%d] 부모에게 종료 메시지 전송 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
    }
    
    if (close(clnt_sock) == -1)
    {
        fprintf(stderr, "  [자식 #%d] close(clnt_sock) error: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
    }
    
    if (close(parent_pipe) == -1)
    {
        fprintf(stderr, "  [자식 #%d] close(parent_pipe) error: %s\n", 
                session_id, strerror(errno));
    }
}

// 서버 소켓 생성
int
create_server_socket(void)
{
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
    {
        fprintf(stderr, "[에러] socket() 생성 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        return -1;
    }
    
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
    {
        fprintf(stderr, "[에러] setsockopt(SO_REUSEADDR) 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        close(serv_sock);
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        if (errno == EADDRINUSE)
        {
            fprintf(stderr, "[에러] bind() 실패: 포트 %d가 이미 사용 중 (EADDRINUSE)\n", PORT);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "[에러] bind() 실패: 권한 없음 (EACCES)\n");
        }
        else
        {
            fprintf(stderr, "[에러] bind() 실패: %s (errno: %d)\n", 
                    strerror(errno), errno);
        }
        close(serv_sock);
        return -1;
    }

    if (listen(serv_sock, 128) == -1)
    {
        fprintf(stderr, "[에러] listen() 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        close(serv_sock);
        return -1;
    }

    printf("[서버] %d 포트 바인딩 및 리스닝 완료\n", PORT);    
    return serv_sock;
}

// 시스템 리소스 출력
void
print_resource_limits(void)
{
    struct rlimit rlim;
    
    if (getrlimit(RLIMIT_NPROC, &rlim) == 0)
    {
        printf("[리소스] 최대 프로세스 수: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한\n");
        else
            printf("%lu (hard: %lu)\n", rlim.rlim_cur, rlim.rlim_max);
    }
    
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        printf("[리소스] 최대 파일 디스크립터: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한\n");
        else
            printf("%lu (hard: %lu)\n", rlim.rlim_cur, rlim.rlim_max);
    }
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
    
    // SIGCHLD 시그널 처리 설정 (좀비 프로세스 방지)
    struct sigaction act;
    act.sa_handler = sigchld_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART | SA_NOCLDSTOP;  // accept()가 시그널에 의해 중단되지 않도록
    if (sigaction(SIGCHLD, &act, 0) == -1)
    {
        fprintf(stderr, "[에러] sigaction(SIGCHLD) 설정 실패: %s\n", strerror(errno));
        exit(1);
    }

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
    
    print_resource_limits();

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
            // ========== 자식 프로세스 ==========
            close(serv_sock);  // 자식은 서버 소켓 필요 없음
            close(sv[0]);      // 부모쪽 파이프 닫기
            
            // 클라이언트 처리
            handle_client(clnt_sock, session_id, clnt_addr, sv[1]);
            
            // 처리 완료 후 자식 프로세스를 종료하지 않고 유지 (fork 고갈 테스트용)
            printf("  [자식 #%d (PID:%d)] 프로세스 유지 중 (fork 고갈 테스트)\n", 
                   session_id, getpid());
            
            // 무한 sleep으로 프로세스 유지 - 종료하지 않음!
            while (1)
            {
                sleep(3600);  // 1시간씩 sleep
            }
            // exit(0);  ← 주석 처리! 프로세스가 종료되지 않고 계속 살아있음
        }
        else
        {
            // ========== 부모 프로세스 ==========
            total_forks++;
            close(clnt_sock);  // 부모는 클라이언트 소켓 필요 없음
            close(sv[1]);      // 자식쪽 파이프 닫기
            
            printf("[서버] 자식 프로세스 생성 성공 (PID: %d, Session #%d)\n", 
                   pid, session_id);
            printf("[통계] 현재까지 생성된 자식 프로세스: %d개\n\n", total_forks);
            
            // 자식으로부터 메시지 읽기 (non-blocking)
            int flags = fcntl(sv[0], F_GETFL, 0);
            fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
            
            int read_len = read(sv[0], parent_buf, BUF_SIZE - 1);
            if (read_len > 0)
            {
                parent_buf[read_len] = 0;
                printf("[부모] 자식으로부터 메시지: %s\n", parent_buf);
            }
            
            close(sv[0]);  // 부모쪽 파이프 닫기
        }
    }

    printf("\n[서버] 정상 종료 중...\n");
    
    if (close(serv_sock) == -1)
    {
        fprintf(stderr, "[에러] close(serv_sock) 실패: %s\n", strerror(errno));
    }
    
    printf("[서버] 서버 소켓 닫기 완료\n");
}