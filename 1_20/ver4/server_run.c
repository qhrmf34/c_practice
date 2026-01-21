#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

void
run_server(void)
{
    int serv_sock, clnt_sock, session_id = 0;
    struct sockaddr_in clnt_addr;
    
    ServerState state = {0};
    state.running = 1;
    state.start_time = time(NULL);
    state.parent_pid = getpid();
    
    setup_signal_handlers(&state);
    log_init();
    
    log_message(LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    serv_sock = create_server_socket();
    if (serv_sock == -1) 
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        return;
    }
    
    log_message(LOG_INFO, "클라이언트 연결 대기 중");
    
    struct pollfd pfd = {.fd = serv_sock, .events = POLLIN, .revents = 0};
    
    while (state.running) 
    {
        handle_child_died(&state);
        
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 1000);
        
        if (ret == -1) 
        {
            if (errno == EINTR)
                continue;
            log_message(LOG_ERROR, "poll() 실패: %s", strerror(errno));
            continue;
        }
        else if (ret == 0)
            continue;
        
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            log_message(LOG_ERROR, "서버 소켓 에러");
            continue;
        }
        else if (pfd.revents & POLLIN) 
        {
            clnt_sock = accept_client(serv_sock, &clnt_addr, &state);
            if (clnt_sock == -1)
                continue;
            
            if (!state.running) 
            {
                log_message(LOG_INFO, "종료 중이므로 새 연결 거부");
                close(clnt_sock);
                continue;
            }
            
            session_id++;
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clnt_addr.sin_addr, client_ip, sizeof(client_ip));
            log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d)", client_ip, ntohs(clnt_addr.sin_port), session_id);
            
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state) == -1) 
            {
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
    }
    
    shutdown_workers(&state);
    
    if (close(serv_sock) == -1)
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    else
        log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    final_cleanup(&state);
}
