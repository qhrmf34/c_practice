#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

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
        printf("[클라이언트 %d] socket() error\n", info->thread_id);
        return NULL;
    }
    
    // 서버 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(info->ip);
    serv_addr.sin_port = htons(info->port);
    
    // 서버 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        printf("[클라이언트 %d] connect() error\n", info->thread_id);
        close(sock);
        return NULL;
    }
    
    printf("[클라이언트 %d] Connected to server (fd: %d)\n", info->thread_id, sock);
    
    // 메시지 송수신 - 10번 반복
    while (count < 10) {
        // 메시지 생성
        sprintf(msg, " Message #%d\n", count + 1);
        
        write(sock, msg, strlen(msg));
        printf("[클라이언트 %d] Sent: %s", info->thread_id, msg);
        
        sleep(1);
        
        // 수신
        str_len = read(sock, recv_buf, BUF_SIZE - 1);
        if (str_len <= 0) {
            printf("[클라이언트 %d] Server disconnected\n", info->thread_id);
            break;
        }
        
        recv_buf[str_len] = 0;
        printf("[클라이언트 %d] Received: %s", info->thread_id, recv_buf);
        
        count++;
    }
    
    printf("[클라이언트 %d] Closing connection (fd: %d)\n", info->thread_id, sock);
    close(sock);
    return NULL;
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void client_run(int argc, char *argv[]) {
    pthread_t threads[MAX_THREADS];
    ThreadArg args[MAX_THREADS];
    int i;
    
    if (argc != 3) {
        printf("%s <IP> <port>\n", argv[0]);
        exit(1);
    }

    printf("=== Multi-Thread Client ===\n");
    printf("서버: %s:%s\n", argv[1], argv[2]);
    printf("Creating %d connection threads...\n\n", MAX_THREADS);
    
    // 클라이언트 스레드 생성
    for (i = 0; i < MAX_THREADS; i++) {
        args[i].ip = argv[1];
        args[i].port = atoi(argv[2]);
        args[i].thread_id = i + 1;
        
        if (pthread_create(&threads[i], NULL, client_thread, (void*)&args[i]) != 0) {
            error_handling("pthread_create() error");
        }
    }    
    
    // 모든 스레드 종료 대기
    for (i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n모든 스레드 연결 종료.\n");
}