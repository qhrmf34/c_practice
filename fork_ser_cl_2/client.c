#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

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
    
    // 클라이언트 스레드 생성
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
    
    printf("\n모든 스레드 연결 종료.\n");
    return 0;
}