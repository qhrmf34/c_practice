#define _POSIX_C_SOURCE 200809L
#include "client_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>

static ClientState *g_client_state = NULL;

static void
signal_handler(int signo)
{
    int saved_errno = errno;
    
    if (signo == SIGINT) 
    {
        if (g_client_state)
            g_client_state->running = 0;
        const char msg[] = "\n[클라이언트] 종료 시그널 수신\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
    
    errno = saved_errno;
}

void
client_run(const char *ip, int port, int client_id, ClientState *state)
{
    int sock, count = 0;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE], recv_buf[BUF_SIZE];
    time_t start_time, end_time;
    
    g_client_state = state;
    start_time = time(NULL);
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGINT) 실패\n", client_id);
        return;
    }
    
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGPIPE) 실패\n", client_id);
        return;
    }
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] socket() 실패: %s\n", client_id, strerror(errno));
        return;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) 
    {
        fprintf(stderr, "[클라이언트 #%d] 잘못된 IP 주소: %s\n", client_id, ip);
        close(sock);
        return;
    }
    serv_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] connect() 실패: %s\n", client_id, strerror(errno));
        close(sock);
        return;
    }
    
    printf("[클라이언트 #%d] 서버 연결 성공!\n", client_id);
    
    struct pollfd write_pfd = {.fd = sock, .events = POLLOUT, .revents = 0};
    struct pollfd read_pfd = {.fd = sock, .events = POLLIN, .revents = 0};
    
    while (count < IO_COUNT && state->running) 
    {
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 중단 (%d/%d 완료)\n", client_id, count, IO_COUNT);
            goto cleanup;
        }
        
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
        
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        
        while (sent < msg_len && state->running) 
        {
            write_pfd.revents = 0;
            int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
            
            if (write_ret == -1) 
            {
                if (errno == EINTR) 
                {
                    if (!state->running) 
                    {
                        printf("[클라이언트 #%d] 전송 중 중단\n", client_id);
                        goto cleanup;
                    }
                    continue;
                }
                fprintf(stderr, "[클라이언트 #%d] write poll 에러\n", client_id);
                goto cleanup;
            }
            else if (write_ret == 0) 
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 타임아웃\n", client_id);
                goto cleanup;
            }
            
            if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 에러 이벤트\n", client_id);
                goto cleanup;
            }
            else if (write_pfd.revents & POLLOUT) 
            {
                ssize_t write_result = write(sock, msg + sent, msg_len - sent);
                
                if (write_result == -1) 
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    if (errno == EPIPE) 
                    {
                        fprintf(stderr, "[클라이언트 #%d] EPIPE: 서버 연결 끊김\n", client_id);
                        goto cleanup;
                    }
                    fprintf(stderr, "[클라이언트 #%d] write() 실패\n", client_id);
                    goto cleanup;
                }
                sent += write_result;
            }
        }
        
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 전송 완료 후 중단\n", client_id);
            goto cleanup;
        }
        
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_ret == -1) 
        {
            if (errno == EINTR) 
            {
                if (!state->running) 
                {
                    printf("[클라이언트 #%d] 수신 대기 중 중단\n", client_id);
                    goto cleanup;
                }
                continue;
            }
            fprintf(stderr, "[클라이언트 #%d] read poll 에러\n", client_id);
            goto cleanup;
        }
        else if (read_ret == 0) 
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 타임아웃\n", client_id);
            goto cleanup;
        }
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 에러 이벤트\n", client_id);
            goto cleanup;
        }
        else if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = read(sock, recv_buf, BUF_SIZE - 1);
            
            if (str_len > 0) 
            {
                recv_buf[str_len] = 0;
                printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
            } 
            else if (str_len == 0) 
            {
                fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료\n", client_id);
                goto cleanup;
            } 
            else 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                fprintf(stderr, "[클라이언트 #%d] read() 실패\n", client_id);
                goto cleanup;
            }
        }
        count++;
    }

cleanup:
    end_time = time(NULL);
    
    if (state->running)
        printf("[클라이언트 #%d] 완료: %d I/O, %ld초\n", client_id, count, end_time - start_time);
    else
        printf("[클라이언트 #%d] 중단: %d/%d I/O, %ld초\n", client_id, count, IO_COUNT, end_time - start_time);
    
    close(sock);
}
