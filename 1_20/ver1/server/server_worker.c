#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

// Echo 처리 함수
static int 
process_echo(SessionDescriptor *session, char *buf, ssize_t len)
{
    struct pollfd write_pfd = {.fd = session->sock, .events = POLLOUT, .revents = 0};
    
    while (1) 
    {
        int poll_ret = do_poll_wait(&write_pfd, POLL_TIMEOUT, "write poll");
        if (poll_ret == 1)
            continue; // 타임아웃/재시도
        if (poll_ret == -1)
            return -1; // 에러
        if (write_pfd.revents & POLLOUT) 
        {
            ssize_t result = safe_write_all(session->sock, buf, len, session);
            if (result == -2)
                continue; // 재시도
            if (result == -1)
                return -1; // 에러
            return 0; // 성공
        }
    }
}

void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, WorkerState *wstate)
{
    printf("\n[자식 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 1;
    monitor.total_sessions = 1;
    
    SessionDescriptor session = {0};
    session.sock = client_sock;
    session.addr = client_addr;
    session.session_id = session_id;
    session.state = SESSION_ACTIVE;
    session.start_time = time(NULL);
    session.last_activity = time(NULL);
    session.io_count = 0;
    
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    struct pollfd read_pfd = {.fd = client_sock, .events = POLLIN, .revents = 0};
    char buf[BUF_SIZE];
    
    while (session.io_count < IO_TARGET && session.state == SESSION_ACTIVE && !wstate->shutdown_requested) 
    {
        if (check_session_timeout(&session)) 
        {
            session.state = SESSION_CLOSING;
            continue;
        }
        int poll_ret = do_poll_wait(&read_pfd, POLL_TIMEOUT, "read poll");
        if (poll_ret == 1)
            continue; // 타임아웃
        if (poll_ret == -1) 
        {
            session.state = SESSION_CLOSING;
            continue;
        }
        if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = safe_read(session.sock, buf, BUF_SIZE - 1, &session);
            if (str_len == 0) 
            { // EOF
                printf("[자식 #%d] 클라이언트 연결 종료\n", session_id);
                session.state = SESSION_CLOSING;
                continue;
            }
            if (str_len == -2)
                continue; // 재시도
            if (str_len == -1) 
            {
                session.state = SESSION_CLOSING;
                continue;
            }
            buf[str_len] = 0;
            
            if (process_echo(&session, buf, str_len) == -1) 
            {
                session.state = SESSION_CLOSING;
                continue;
            }
            session.io_count++;
            printf("[자식 #%d] I/O 완료: %d/%d\n", session_id, session.io_count, IO_TARGET);
        }
    }
    
    session.state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    if (wstate->shutdown_requested)
        printf("[자식 #%d] SIGTERM으로 종료 - %d I/O 완료, %ld초 소요\n", 
               session_id, session.io_count, end_time - session.start_time);
    else
        printf("[자식 #%d] 처리 완료 - %d I/O 완료, %ld초 소요\n", 
               session_id, session.io_count, end_time - session.start_time);
    
    monitor.active_sessions = 0;
    close(client_sock);
    
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    printf("[자식 #%d (PID:%d)] 종료\n\n", session_id, getpid());
}
