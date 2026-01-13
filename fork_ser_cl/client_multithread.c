#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define MAX_THREADS 5  // 생성할 연결 스레드 개수

typedef struct {
    char *ip;
    int port;
    int thread_id;
} ThreadArg;

void *client_thread(void *arg);
void error_handling(char *message);

int main(int argc, char *argv[]) {
    pthread_t threads[MAX_THREADS];
    ThreadArg args[MAX_THREADS];
    int i;
    
    if (argc != 3) {
        printf("Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    
    printf("=== Multi-Thread Client ===\n");
    printf("Server: %s:%s\n", argv[1], argv[2]);
    printf("Creating %d connection threads...\n\n", MAX_THREADS);
    
    // 여러 개의 클라이언트 스레드 생성
    for (i = 0; i < MAX_THREADS; i++) {
        args[i].ip = argv[1];
        args[i].port = atoi(argv[2]);
        args[i].thread_id = i + 1;
        
        if (pthread_create(&threads[i], NULL, client_thread, (void*)&args[i]) != 0) {
            error_handling("pthread_create() error");
        }
        
        sleep(1);  // 스레드 생성 간격
    }
    
    // 모든 스레드 종료 대기
    for (i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAll threads terminated.\n");
    return 0;
}

void *client_thread(void *arg) {
    ThreadArg *info = (ThreadArg*)arg;
    int sock;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    int str_len;
    int count = 0;
    
    // 소켓 생성
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("[Thread %d] socket() error\n", info->thread_id);
        return NULL;
    }
    
    // 서버 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(info->ip);
    serv_addr.sin_port = htons(info->port);
    
    // 서버 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        printf("[Thread %d] connect() error\n", info->thread_id);
        close(sock);
        return NULL;
    }
    
    printf("[Thread %d] Connected to server (fd: %d)\n", info->thread_id, sock);
    
    // 메시지 송수신 - 1초 간격
    while (count < 10) {  // 10번 반복
        // 메시지 생성
        sprintf(msg, "[Thread %d] Message #%d\n", info->thread_id, count + 1);
        
        // 송신
        write(sock, msg, strlen(msg));
        printf("[Thread %d] Sent: %s", info->thread_id, msg);
        
        // 1초 대기 (서버의 thread I/O 간격과 동기화)
        sleep(1);
        
        // 수신
        str_len = read(sock, recv_buf, BUF_SIZE - 1);
        if (str_len <= 0) {
            printf("[Thread %d] Server disconnected\n", info->thread_id);
            break;
        }
        
        recv_buf[str_len] = 0;
        printf("[Thread %d] Received: %s", info->thread_id, recv_buf);
        
        count++;
        sleep(1);  // 다음 메시지까지 1초 대기
    }
    
    printf("[Thread %d] Closing connection (fd: %d)\n", info->thread_id, sock);
    close(sock);
    return NULL;
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}