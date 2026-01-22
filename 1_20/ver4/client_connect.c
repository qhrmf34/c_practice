#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
client_connect(int argc, char *argv[])
{
    char *ip;
    int port, client_id, iteration = 0;
    
    if (argc != 3 && argc != 4) 
    {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        exit(1);
    }
    
    ip = argv[1];
    port = atoi(argv[2]);
    
    if (port <= 0 || port > 65535) 
    {
        fprintf(stderr, "Error: Invalid port number %d\n", port);
        exit(1);
    }
    
    if (argc == 4)
        client_id = atoi(argv[3]);
    else
        client_id = getpid() % 1000;
    
    printf("=== 클라이언트 #%d 시작 ===\n", client_id);
    printf("서버: %s:%d\n", ip, port);
    printf("Ctrl+C로 종료하세요.\n\n");
    
    ClientState state = {0};
    state.running = 1;
    
    while (state.running) 
    {
        iteration++;
        printf("\n[클라이언트 #%d] ===== 반복 #%d =====\n", client_id, iteration);
        client_run(ip, port, client_id, &state);
    }
    return 0;
}
