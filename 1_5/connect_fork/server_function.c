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

// 서버 실행 플래그
static volatile sig_atomic_t server_running = 1;

// 좀비 프로세스 방지를 위한 signal handler
static void 
sigchld_handler(int signo) 
{
    // 종료된 자식 프로세스 정리
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// 서버 종료 signal handler
static void 
shutdown_handler(int signo) 
{
    server_running = 0;
    printf("\n[서버] Shutdown...\n");

}

// 클라이언트 처리 함수 (자식 프로세스에서 실행)
void 
handle_client(int clnt_sock, int session_id, struct sockaddr_in clnt_addr) 
{
    char buf[BUF_SIZE];
    int str_len;
    int count = 0;

    printf(" [자식 프로세스 #%d (PID: %d)] 클라이언트 연결 (fd: %d, %s:%d)\n",
           session_id, getpid(), clnt_sock, 
           inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

    // 클라이언트가 연결을 끊을 때까지 계속 처리
    while (1) 
    { 
        str_len = read(clnt_sock, buf, BUF_SIZE - 1);
        
        if (str_len <= 0) 
        {
            if (str_len == 0)
                printf(" [자식 프로세스 #%d] 클라이언트 연결 종료\n", session_id);
            else
                perror("read() error");
            break;
        }

        buf[str_len] = 0;
        printf("  [자식 프로세스 #%d] Received: %s", session_id, buf);

        // Echo back
        write(clnt_sock, buf, str_len);
        count++;
    }

    printf("  [자식 프로세스 #%d (PID: %d)] %d I/O operations. Closing connection.\n", 
           session_id, getpid(), count);
    
    close(clnt_sock);
    
    printf("  [Child Process #%d (PID: %d)] Terminating.\n", session_id, getpid());
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
        perror("socket() error");
        return -1;
    }

    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) 
    {
        perror("bind() error");
        close(serv_sock);
        return -1;
    }

    if (listen(serv_sock, 128) == -1) 
    {
        perror("listen() error");
        close(serv_sock);
        return -1; 
    }

    printf("[서버] %d 포트 준비\n", PORT);
    
    return serv_sock;
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
    
    // SIGCHLD 시그널 처리 설정 (좀비 프로세스 방지)
    struct sigaction act;
    act.sa_handler = sigchld_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;  // accept()가 시그널에 의해 중단되지 않도록
    sigaction(SIGCHLD, &act, 0);

    // SIGINT, SIGTERM 시그널 처리
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler;
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    sigaction(SIGINT, &shutdown_act, 0);   // Ctrl+C
    sigaction(SIGTERM, &shutdown_act, 0);  // kill 명령

    printf("=== Multi-Process Server ===\n");
    printf("Port: %d\n", PORT);

    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1) 
    {
        perror("서버 소켓 생성 실패");
        exit(1);
    }

    printf("서버에서 연결을 대기중\n");

    // 메인 루프: accept() 후 fork()
    while (server_running) 
    {
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
        
        if (clnt_sock == -1) 
        {
            if (!server_running) 
            {
                // 종료 시그널로 인한 중단
                break;
            }
            perror("accept() error");
            continue;
        }

        session_id++;
        printf("[서버] 새 연결 : %s:%d (Session #%d)\n",
               inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), session_id);

        // fork: 자식 프로세스 생성
        pid = fork();
        
        if (pid == -1) 
        {
            perror("fork() error");
            close(clnt_sock);
            continue;
        }
        
        if (pid == 0) 
        {
            // 자식 프로세스
            close(serv_sock);  // 자식은 서버 소켓 필요 없음
            
            // 클라이언트 처리 (I/O 10번)
            handle_client(clnt_sock, session_id, clnt_addr);
            
            // 처리 완료 후 자식 프로세스 종료
            exit(0);
        } 
        else 
        {
            // 부모 프로세스
            close(clnt_sock);  // 부모는 클라이언트 소켓 필요 없음
            printf("[서버] 자식 프로세스 (PID: %d) Session #%d\n\n", pid, session_id);
        }
    }

    printf("서버 종료.\n");
    close(serv_sock);
}
