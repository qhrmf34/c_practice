#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_SESSIONS 32

typedef struct {
    int sock;
    struct sockaddr_in addr;
} ClientInfo;

void error_handling(char *message);
void read_childproc(int sig);
void *handle_client_thread(void *arg);

int session_count = 0;

int main(int argc, char *argv[]) {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    pid_t pid;
    struct sigaction act;
    socklen_t addr_size;
    
    // SIGCHLD 시그널 처리 (좀비 프로세스 방지)
    act.sa_handler = read_childproc;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGCHLD, &act, 0);
    
    // 소켓 생성
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");
    
    // SO_REUSEADDR 옵션
    int option = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    
    // 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);
    
    // 바인드
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");
    
    // 리슨
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    printf("=== Multi-Process Echo Server ===\n");
    printf("Port: %d\n", PORT);
    printf("Max Sessions: %d\n", MAX_SESSIONS);
    printf("\n[Mother Process] Waiting for clients...\n\n");
    
    // M.P - accept 루프
    while(1) {
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
        
        if (clnt_sock == -1) {
            continue;
        }
        
        // 세션 수 체크
        if (session_count >= MAX_SESSIONS) {
            printf("[Mother Process] Session limit reached (%d/%d)! Rejecting connection.\n", 
                   session_count, MAX_SESSIONS);
            char *msg = "Server is full (32/32). Try again later.\n";
            write(clnt_sock, msg, strlen(msg));
            close(clnt_sock);
            continue;
        }
        
        session_count++;
        printf("[Mother Process] Client connected: %s:%d\n", 
               inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
        printf("[Mother Process] ① sock sess (fd: %d), ② address (%s:%d)\n",
               clnt_sock, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
        printf("[Mother Process] Active sessions: %d/%d\n", session_count, MAX_SESSIONS);
        printf("[Mother Process] Creating child process...\n");
        
        // child create (fork)
        pid = fork();
        
        if (pid == -1) {
            close(clnt_sock);
            session_count--;
            continue;
        }
        
        if (pid == 0) {  // ===== Child Process (sess 0) =====
            close(serv_sock);  // 자식은 서버 소켓 닫음
            
            printf("\n[Child Process %d] Started (sess 0)\n", getpid());
            printf("[Child Process %d] Handling client (fd: %d)\n", getpid(), clnt_sock);
            
            // 클라이언트 정보를 스레드에 전달
            ClientInfo *client = (ClientInfo*)malloc(sizeof(ClientInfo));
            client->sock = clnt_sock;
            client->addr = clnt_addr;
            
            // thread I/O 시작
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_thread, (void*)client);
            pthread_join(thread, NULL);  // 스레드 종료 대기
            
            printf("[Child Process %d] Client disconnected, terminating\n", getpid());
            exit(0);  // 자식 프로세스 종료
        }
        else {  // ===== Parent Process (Mother Process) - sess(a) =====
            close(clnt_sock);  // Parent sess(a) → close(x)
            printf("[Mother Process] Parent sess(a) closed (x)\n");
            printf("[Mother Process] Back to accept loop...\n\n");
        }
    }
    
    close(serv_sock);
    return 0;
}

void *handle_client_thread(void *arg) {
    ClientInfo *client = (ClientInfo*)arg;
    char buf[BUF_SIZE];
    int str_len;
    
    printf("[Thread in Child %d] Thread I/O started (fd: %d)\n", getpid(), client->sock);
    
    // Thread I/O - 1초 간격
    while(1) {
        // 1초 대기 후 receive
        sleep(1);
        
        str_len = read(client->sock, buf, BUF_SIZE);
        if (str_len <= 0) {
            break;  // 연결 종료
        }
        
        buf[str_len] = 0;
        printf("[Thread in Child %d] Received: %s", getpid(), buf);
        
        // 1초 대기 후 send
        sleep(1);
        write(client->sock, buf, str_len);  // Echo back
    }
    
    close(client->sock);
    free(client);
    return NULL;
}

void read_childproc(int sig) {
    pid_t pid;
    int status;
    
    // 종료된 자식 프로세스 정리 (좀비 프로세스 방지)
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        session_count--;
        printf("[Mother Process] Child %d terminated. Active sessions: %d/%d\n", 
               pid, session_count, MAX_SESSIONS);
    }
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}