#include "client_function.h"
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int 
client_connect(int argc, char *argv[]) 
{
    char *ip;
    int port, iteration = 0;
    
    if (argc != 3 && argc != 4) 
    {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        exit(1);
    }
    
    ip = argv[1];
    port = atoi(argv[2]);
    
    ClientContext ctx = {.running = 1};
    if (argc == 4)
        ctx.client_id = atoi(argv[3]);
    else
        ctx.client_id = getpid() % 1000;
    
    setup_client_signal_handlers(&ctx);
    
    printf("=== 클라이언트 #%d 시작 ===\n", ctx.client_id);
    printf("서버: %s:%d\n", ip, port);
    printf("Ctrl+C로 종료\n\n");
    
    while (ctx.running) 
    {
        iteration++;
        printf("\n[클라이언트 #%d] === 반복 #%d ===\n", ctx.client_id, iteration);
        client_run(ip, port, &ctx);
    }
    
    return 0;
}
