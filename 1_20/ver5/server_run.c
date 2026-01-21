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
    
    log_init();
    setup_signal_handlers(&state);
    
    log_message(LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    serv_sock = create_server_socket();
    if (serv_sock == -1) {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        return;
    }
    
    log_message(LOG_INFO, "클라이언트 연결 대기 중");
    
    struct pollfd pfds[2];
    pfds[0].fd = serv_sock;
    pfds[0].events = POLLIN;
    pfds[1].fd = state.signal_pipe[0];
    pfds[1].events = POLLIN;
    
    while (state.running) {
        pfds[0].revents = 0;
        pfds[1].revents = 0;
        
        int ret = poll(pfds, 2, 1000);
        
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            log_message(LOG_ERROR, "poll() 실패: %s", strerror(errno));
            continue;
        }
        
        if (ret == 0)
            continue;
        
        if (pfds[1].revents & POLLIN) {
            unsigned char signo;
            while (read(state.signal_pipe[0], &signo, 1) > 0)
                handle_signal(&state, signo);
        }
        
        if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_message(LOG_ERROR, "서버 소켓 에러, 재시작 시도");
            close(serv_sock);
            serv_sock = create_server_socket();
            if (serv_sock == -1) {
                log_message(LOG_ERROR, "서버 소켓 재생성 실패");
                continue;
            }
            pfds[0].fd = serv_sock;
            continue;
        }
        
        if (pfds[0].revents & POLLIN) {
            clnt_sock = accept_client(serv_sock, &clnt_addr);
            if (clnt_sock == -1)
                continue;
            
            if (!state.running) {
                log_message(LOG_INFO, "종료 중이므로 새 연결 거부");
                close(clnt_sock);
                continue;
            }
            
            session_id++;
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clnt_addr.sin_addr, client_ip, sizeof(client_ip));
            log_message(LOG_INFO, "새 연결: %s:%d (Session #%d)", client_ip, ntohs(clnt_addr.sin_port), session_id);
            
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state) == -1) {
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
        }
    }
    
    log_message(LOG_INFO, "서버 종료 시작");
    shutdown_all_workers(&state);
    
    close(serv_sock);
    close(state.signal_pipe[0]);
    close(state.signal_pipe[1]);
    
    print_resource_limits();
    log_message(LOG_INFO, "=== 서버 정상 종료 완료 ===");
}
