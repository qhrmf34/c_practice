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
    {  // Ctrl+C
        if (g_client_state) g_client_state->running = 0;
        const char msg[] = "\n[클라이언트] 종료 시그널 수신 (Ctrl+C)\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    } 
    else if (signo == SIGPIPE) 
    {
        // SIGPIPE는 무시
    }
    
    errno = saved_errno;
}

static int poll_socket(int sock, short events, int timeout) 
{
    struct pollfd pfd = {.fd = sock, .events = events, .revents = 0};
    int ret = poll(&pfd, 1, timeout);
    
    if (ret == -1) 
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
            return -2;

        return -1;
    }
    if (ret == 0) 
        return 0;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        return -3;
    if (pfd.revents & events) 
        return 1;

    return 0;
}

static int 
send_message(int sock, const char *msg, int msg_len, int client_id, ClientState *state) {
    ssize_t sent = 0;
    
    while (sent < msg_len && state->running) 
    {
        int ret = poll_socket(sock, POLLOUT, POLL_TIMEOUT);
        
        if (ret == -2) 
        {
            if (!state->running) 
            {
                printf("[클라이언트 #%d] 전송 중 중단됨\n", client_id);
                return -1;
            }
            printf("[클라이언트 #%d] write poll interrupted, 재시도\n", client_id);
            continue;
        }
        if (ret == -1) 
        {
            fprintf(stderr, "[클라이언트 #%d] write poll() error: %s\n", client_id, strerror(errno));
            return -1;
        }
        if (ret == 0) 
        {
            fprintf(stderr, "[클라이언트 #%d] write poll 타임아웃\n", client_id);
            return -1;
        }
        if (ret < 0) 
        {
            fprintf(stderr, "[클라이언트 #%d] write poll 에러 이벤트\n", client_id);
            return -1;
        }
        
        ssize_t write_result = write(sock, msg + sent, msg_len - sent);
        
        if (write_result == -1) 
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EPIPE) 
            {
                fprintf(stderr, "[클라이언트 #%d] write() EPIPE: 서버 연결 끊김\n", client_id);
                return -1;
            }
            fprintf(stderr, "[클라이언트 #%d] write() 실패: %s\n", client_id, strerror(errno));
            return -1;
        }
        sent += write_result;
    }
    return sent;
}

static int 
receive_message(int sock, char *recv_buf, int client_id, ClientState *state) 
{
    int ret = poll_socket(sock, POLLIN, POLL_TIMEOUT);
    
    if (ret == -2) {
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 수신 대기 중 중단됨\n", client_id);
            return -1;
        }
        printf("[클라이언트 #%d] read poll interrupted, 재시도\n", client_id);
        return -2;
    }
    if (ret == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] read poll() error: %s\n", client_id, strerror(errno));
        return -1;
    }
    if (ret == 0) 
    {
        fprintf(stderr, "[클라이언트 #%d] read poll 타임아웃\n", client_id);
        return -1;
    }
    if (ret < 0) 
    {
        fprintf(stderr, "[클라이언트 #%d] read poll 에러 이벤트\n", client_id);
        return -1;
    }
    
    ssize_t str_len = read(sock, recv_buf, BUF_SIZE - 1);
    
    if (str_len > 0) 
    {
        recv_buf[str_len] = 0;
        printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
        return str_len;
    }
    if (str_len == 0) 
    {
        fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
        return 0;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return -2;
    fprintf(stderr, "[클라이언트 #%d] read() 실패: %s\n", client_id, strerror(errno));
    return -1;
}

void 
client_run(const char *ip, int port, int client_id, ClientState *state) 
{
    int sock;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    int count = 0;
    time_t start_time, end_time;
    
    g_client_state = state;
    start_time = time(NULL);
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGINT) 설정 실패: %s\n", client_id, strerror(errno));
        return;
    }
    
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGPIPE) 설정 실패: %s\n", client_id, strerror(errno));
        return;
    }
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) 
    {
        fprintf(stderr, "[클라이언트 #%d] socket() 생성 실패: %s\n", client_id, strerror(errno));
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
    
    printf("[클라이언트 #%d] 서버 연결 성공! 10번 I/O 시작...\n", client_id);
    
    while (count < IO_COUNT && state->running) 
    {
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 사용자 요청으로 중단 (%d/%d I/O 완료)\n", client_id, count, IO_COUNT);
            goto cleanup;
        }
        
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
        
        int send_ret = send_message(sock, msg, strlen(msg), client_id, state);
        if (send_ret <= 0) goto cleanup;
        
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 전송 완료 후 중단\n", client_id);
            goto cleanup;
        }
        
        int recv_ret = receive_message(sock, recv_buf, client_id, state);
        if (recv_ret == -2) 
            continue;
        if (recv_ret <= 0) 
            goto cleanup;
        
        count++;
    }

cleanup:
    end_time = time(NULL);
    
    if (state->running)
        printf("[클라이언트 #%d] 완료: %d I/O, %ld초 소요\n", client_id, count, end_time - start_time);
    else
        printf("[클라이언트 #%d] 중단됨: %d/%d I/O 완료, %ld초 소요\n", client_id, count, IO_COUNT, end_time - start_time);
    
    close(sock);
}