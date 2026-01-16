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

void
client_run(const char *ip, int port, int client_id)
{
    int sock;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    ssize_t str_len;
    ssize_t write_result;
    int count = 0;
    time_t start_time, end_time;
    
    start_time = time(NULL);
    
    // 소켓 생성 (기본 blocking 모드)
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] socket() 생성 실패: %s\n", 
                client_id, strerror(errno));
        return;
    }
    
    // 서버 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "[클라이언트 #%d] 잘못된 IP 주소: %s\n", client_id, ip);
        close(sock);
        return;
    }
    
    serv_addr.sin_port = htons(port);
    
    // 서버 연결 (blocking)
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] connect() 실패: %s\n", 
                client_id, strerror(errno));
        close(sock);
        return;
    }
    
    printf("[클라이언트 #%d] 서버 연결 성공! 10번 I/O 시작...\n", client_id);
    
    // 메시지 송수신 - IO_COUNT번 반복
    while (count < IO_COUNT)
    {
        // 메시지 생성
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", 
                 client_id, count + 1, time(NULL));
        
        // 메시지 전송 (blocking - partial write 처리)
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        
        while (sent < msg_len)
        {
            write_result = write(sock, msg + sent, msg_len - sent);
            
            if (write_result == -1)
            {
                // EINTR, EAGAIN, EWOULDBLOCK은 재시도
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                fprintf(stderr, "[클라이언트 #%d] write() 실패: %s\n", 
                        client_id, strerror(errno));
                close(sock);
                return;
            }
            sent += write_result;
        }
        
        // 에코 메시지 수신 (blocking)
        str_len = read(sock, recv_buf, BUF_SIZE - 1);
        
        if (str_len > 0)
        {
            recv_buf[str_len] = 0;
            printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
        }
        else if (str_len == 0)
        {
            fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
            close(sock);
            return;
        }
        else
        {
            // EINTR, EAGAIN, EWOULDBLOCK은 재시도
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            fprintf(stderr, "[클라이언트 #%d] read() 실패: %s\n", 
                    client_id, strerror(errno));
            close(sock);
            return;
        }
        
        count++;
    }
    
    end_time = time(NULL);
    
    printf("[클라이언트 #%d] 완료: %d I/O, %ld초 소요\n", 
           client_id, count, end_time - start_time);
    
    close(sock);
}