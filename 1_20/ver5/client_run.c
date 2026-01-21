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

void
client_run(const char *ip, int port, int client_id)
{
    int sock, count = 0;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE], recv_buf[BUF_SIZE];
    time_t start_time, end_time;
    
    start_time = time(NULL);
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        fprintf(stderr, "[클라이언트 #%d] socket() 실패\n", client_id);
        return;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);
    serv_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        fprintf(stderr, "[클라이언트 #%d] connect() 실패\n", client_id);
        close(sock);
        return;
    }
    
    printf("[클라이언트 #%d] 연결 성공\n", client_id);
    
    struct pollfd write_pfd = {.fd = sock, .events = POLLOUT};
    struct pollfd read_pfd = {.fd = sock, .events = POLLIN};
    
    while (count < IO_COUNT) {
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
        
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        
        while (sent < msg_len) {
            write_pfd.revents = 0;
            int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
            
            if (write_ret <= 0)
                goto cleanup;
            
            if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                goto cleanup;
            
            if (write_pfd.revents & POLLOUT) {
                ssize_t n = write(sock, msg + sent, msg_len - sent);
                if (n <= 0)
                    goto cleanup;
                sent += n;
            }
        }
        
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_ret <= 0)
            goto cleanup;
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            goto cleanup;
        
        if (read_pfd.revents & POLLIN) {
            ssize_t n = read(sock, recv_buf, BUF_SIZE - 1);
            if (n <= 0)
                goto cleanup;
            recv_buf[n] = 0;
            printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
        }
        
        count++;
    }

cleanup:
    end_time = time(NULL);
    printf("[클라이언트 #%d] 완료: %d I/O, %ld초\n", client_id, count, end_time - start_time);
    close(sock);
}
