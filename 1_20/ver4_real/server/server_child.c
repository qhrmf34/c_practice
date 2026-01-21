#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

void
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    
    printf("\n[자식 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", ip_str, ntohs(client_addr.sin_port));
    
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 1;
    monitor.total_sessions = 1;
    
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (session == NULL) 
    {
        fprintf(stderr, "[자식 #%d] malloc() 실패: %s\n", session_id, strerror(errno));
        close(client_sock);
        return;
    }
    
    memset(session, 0, sizeof(SessionDescriptor));
    session->sock = client_sock;
    session->addr = client_addr;
    session->session_id = session_id;
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    char buf[BUF_SIZE];
    struct pollfd read_pfd = {.fd = session->sock, .events = POLLIN, .revents = 0};
    struct pollfd write_pfd = {.fd = session->sock, .events = POLLOUT, .revents = 0};
 
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE && state->running) 
    {
        time_t current_time = time(NULL);
        time_t idle_duration = current_time - session->last_activity;
        
        if (idle_duration >= SESSION_IDLE_TIMEOUT) 
        {
            fprintf(stderr, "[자식 #%d] idle 타임아웃 (%ld초 무활동)\n", session_id, idle_duration);
            goto cleanup;
        }
        
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_ret == -1) 
        {
            if (errno == EINTR) 
            {
                printf("[자식 #%d] read poll interrupted, 재시도\n", session_id);
                continue;
            }
            fprintf(stderr, "[자식 #%d] read poll() error: %s\n", session_id, strerror(errno));
            goto cleanup;
        }
        else if (read_ret == 0) 
        {
            fprintf(stderr, "[자식 #%d] read poll 타임아웃\n", session_id);
            goto cleanup;
        }
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            log_message(LOG_ERROR, "read poll 에러 이벤트 발생: 0x%x", read_pfd.revents);
            goto cleanup;
        }
        else if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);
        
            if (str_len == 0) 
            {
                printf("[자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
                goto cleanup;
            }
            else if (str_len < 0) 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                fprintf(stderr, "[자식 #%d] read() error: %s\n", session_id, strerror(errno));
                goto cleanup;
            }
            session->last_activity = time(NULL);
            buf[str_len] = 0;
            
            ssize_t sent = 0;
            while (sent < str_len) 
            {
                write_pfd.revents = 0;
                int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
                
                if (write_ret == -1) 
                {
                    if (errno == EINTR) 
                    {
                        printf("[자식 #%d] write poll interrupted, 재시도\n", session_id);
                        continue;
                    }
                    fprintf(stderr, "[자식 #%d] write poll() error: %s\n", session_id, strerror(errno));
                    goto cleanup;
                }
                else if (write_ret == 0) 
                {
                    fprintf(stderr, "[자식 #%d] write poll 타임아웃\n", session_id);
                    goto cleanup;
                }
                
                if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
                {
                    log_message(LOG_ERROR, "write poll 에러 이벤트 발생: 0x%x", write_pfd.revents);
                    goto cleanup;
                }
                else if (write_pfd.revents & POLLOUT) 
                {
                    ssize_t write_result = write(session->sock, buf + sent, str_len - sent);
                    
                    if (write_result == -1) 
                    {
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                            continue;
                        else if (errno == EPIPE) 
                        {
                            fprintf(stderr, "[자식 #%d] write() EPIPE: 클라이언트 연결 끊김\n", session_id);
                            goto cleanup;
                        }
                        fprintf(stderr, "[자식 #%d] write() error: %s\n", session_id, strerror(errno));
                        goto cleanup;
                    }
                    sent += write_result;
                }
            }
            session->io_count++;
            session->last_activity = time(NULL);
            printf("[자식 #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
        }
    }

cleanup:
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    if (!state->running)
        printf("[자식 #%d (PID:%d)] SIGTERM으로 인한 graceful shutdown - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    else
        printf("[자식 #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    
    monitor.active_sessions--;
    
    if (close(client_sock) == -1)
        fprintf(stderr, "[자식 #%d] close(client_sock) 실패: %s\n", session_id, strerror(errno));
    
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    free(session);
    printf("[자식 #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}
