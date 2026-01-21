#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

static int 
check_timeout(SessionDescriptor *session, int session_id) 
{
    time_t current_time = time(NULL);
    time_t idle_duration = current_time - session->last_activity;
    
    if (idle_duration >= SESSION_IDLE_TIMEOUT) 
    {
        fprintf(stderr, "[자식 #%d] idle 타임아웃 (%ld초 무활동)\n", session_id, idle_duration);
        return 1;
    }
    return 0;
}

static int 
read_from_client(SessionDescriptor *session, int session_id, char *buf) 
{
    int ret = poll_socket(session->sock, POLLIN, POLL_TIMEOUT);
    
    if (ret == -2) 
    {  // EINTR
        printf("[자식 #%d] read poll interrupted, 재시도\n", session_id);
        return -2;
    }
    if (ret == -1) 
    {
        fprintf(stderr, "[자식 #%d] read poll() error: %s\n", session_id, strerror(errno));
        return -1;
    }
    if (ret == 0) 
    {
        fprintf(stderr, "[자식 #%d] read poll 타임아웃 (%d초 초과)\n", session_id, POLL_TIMEOUT / 1000);
        return -1;
    }
    if (ret < 0) 
    {
        log_message(LOG_ERROR, "read poll 에러 이벤트 발생");
        return -1;
    }
    
    ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);
    
    if (str_len == 0) 
    {
        printf("[자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
        return 0;
    }
    if (str_len < 0) 
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        fprintf(stderr, "[자식 #%d] read() error: %s\n", session_id, strerror(errno));
        return -1;
    }
    
    buf[str_len] = 0;
    return str_len;
}

static int write_to_client(SessionDescriptor *session, int session_id, const char *buf, int len) {
    ssize_t sent = 0;
    
    while (sent < len) 
    {
        int ret = poll_socket(session->sock, POLLOUT, POLL_TIMEOUT);
        
        if (ret == -2) 
        {
            printf("[자식 #%d] write poll interrupted, 재시도\n", session_id);
            continue;
        }
        if (ret == -1) 
        {
            fprintf(stderr, "[자식 #%d] write poll() error: %s\n", session_id, strerror(errno));
            return -1;
        }
        if (ret == 0) 
        {
            fprintf(stderr, "[자식 #%d] write poll 타임아웃\n", session_id);
            return -1;
        }
        if (ret < 0) 
        {
            log_message(LOG_ERROR, "write poll 에러 이벤트 발생");
            return -1;
        }
        
        ssize_t write_result = write(session->sock, buf + sent, len - sent);
        
        if (write_result == -1) 
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
                continue;
            if (errno == EPIPE) 
            {
                fprintf(stderr, "[자식 #%d] write() EPIPE: 클라이언트 연결 끊김\n", session_id);
                return -1;
            }
            fprintf(stderr, "[자식 #%d] write() error: %s\n", session_id, strerror(errno));
            return -1;
        }
        sent += write_result;
    }
    return sent;
}

void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state) {
    printf("\n[자식 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 0;
    monitor.total_sessions = 0;
    
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
    session->state = SESSION_IDLE;
    
    monitor.active_sessions++;
    monitor.total_sessions++;
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    char buf[BUF_SIZE];
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE && state->running) 
    {
        if (check_timeout(session, session_id)) 
            goto cleanup;
        
        int str_len = read_from_client(session, session_id, buf);
        if (str_len == -2) 
            continue;  // 재시도
        if (str_len <= 0) 
            goto cleanup;
        
        session->last_activity = time(NULL);
        
        int write_result = write_to_client(session, session_id, buf, str_len);
        if (write_result <= 0) 
            goto cleanup;
        
        session->io_count++;
        session->last_activity = time(NULL);
        printf("[자식 #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
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
