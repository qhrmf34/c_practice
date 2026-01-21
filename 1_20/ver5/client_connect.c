#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

static volatile sig_atomic_t client_running = 1;

static void
client_signal_handler(int signo)
{
    (void)signo;
    client_running = 0;
}

int
client_connect(int argc, char *argv[])
{
    char *ip;
    int port, client_id, iteration = 0;
    
    if (argc != 3 && argc != 4) {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        exit(1);
    }
    
    ip = argv[1];
    port = atoi(argv[2]);
    
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        exit(1);
    }
    
    if (argc == 4)
        client_id = atoi(argv[3]);
    else
        client_id = getpid() % 1000;
    
    signal(SIGINT, client_signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    printf("=== 클라이언트 #%d 시작 ===\n", client_id);
    printf("서버: %s:%d\n", ip, port);
    printf("Ctrl+C로 종료\n\n");
    
    while (client_running) {
        iteration++;
        printf("\n[클라이언트 #%d] === 반복 #%d ===\n", client_id, iteration);
        client_run(ip, port, client_id);
    }
    
    return 0;
}
