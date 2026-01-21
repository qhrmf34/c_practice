#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
client_connect(int argc, char *argv[])
{
    char *ip;
    int port;
    int client_id;
    
    if (argc != 3 && argc != 4)
    {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        printf("Example: %s 127.0.0.1 9190\n", argv[0]);
        printf("Example: %s 127.0.0.1 9190 1\n", argv[0]);
        exit(1);
    }
    
    ip = argv[1];
    port = atoi(argv[2]);
    
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Error: Invalid port number %d\n", port);
        exit(1);
    }
    
    // client_id가 주어지지 않으면 PID 사용
    if (argc == 4)
    {
        client_id = atoi(argv[3]);
    }
    else
    {
        client_id = getpid() % 1000;  // PID의 마지막 3자리
    }
    
    printf("=== 클라이언트 #%d 시작 (무한 반복 모드, blocking) ===\n", client_id);
    printf("서버: %s:%d\n", ip, port);
    printf("Connect → 10번 I/O → Close → 반복\n");
    printf("Ctrl+C로 종료하세요.\n\n");
    
    int iteration = 0;
    
    // 무한 루프: connect → 10번 I/O → close → 반복
    while (client_running)
    {
        iteration++;
        printf("\n[클라이언트 #%d] ===== 반복 #%d =====\n", client_id, iteration);
        client_run(ip, port, client_id);
        // sleep 없이 바로 다시 연결
    }
    
    return 0;
}