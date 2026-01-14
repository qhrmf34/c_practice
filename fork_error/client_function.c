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
    int str_len;
    int write_result;
    int count = 0;
    time_t start_time, end_time;
    
    start_time = time(NULL);
    
    // 소켓 생성
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] socket() 생성 실패: %s (errno: %d)\n", 
                client_id, strerror(errno), errno);
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
    
    // 서버 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        if (errno == ECONNREFUSED)
        {
            fprintf(stderr, "[클라이언트 #%d] connect() 실패: 연결 거부됨 (ECONNREFUSED)\n", 
                    client_id);
        }
        else if (errno == ETIMEDOUT)
        {
            fprintf(stderr, "[클라이언트 #%d] connect() 실패: 타임아웃 (ETIMEDOUT)\n", 
                    client_id);
        }
        else if (errno == ENETUNREACH)
        {
            fprintf(stderr, "[클라이언트 #%d] connect() 실패: 네트워크 도달 불가 (ENETUNREACH)\n", 
                    client_id);
        }
        else if (errno == EHOSTUNREACH)
        {
            fprintf(stderr, "[클라이언트 #%d] connect() 실패: 호스트 도달 불가 (EHOSTUNREACH)\n", 
                    client_id);
        }
        else
        {
            fprintf(stderr, "[클라이언트 #%d] connect() 실패: %s (errno: %d)\n", 
                    client_id, strerror(errno), errno);
        }
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
        
        // 메시지 전송
        write_result = write(sock, msg, strlen(msg));
        if (write_result == -1)
        {
            if (errno == EPIPE)
            {
                fprintf(stderr, "[클라이언트 #%d] write() 실패: Broken pipe (EPIPE)\n", 
                        client_id);
            }
            else if (errno == ECONNRESET)
            {
                fprintf(stderr, "[클라이언트 #%d] write() 실패: Connection reset (ECONNRESET)\n", 
                        client_id);
            }
            else if (errno == EINTR)
            {
                fprintf(stderr, "[클라이언트 #%d] write() interrupted (EINTR), 재시도\n", 
                        client_id);
                continue;
            }
            else
            {
                fprintf(stderr, "[클라이언트 #%d] write() 실패: %s (errno: %d)\n", 
                        client_id, strerror(errno), errno);
            }
            break;
        }
        else if (write_result != (int)strlen(msg))
        {
            fprintf(stderr, "[클라이언트 #%d] 부분 write: %d/%lu bytes\n", 
                    client_id, write_result, strlen(msg));
        }
        
        // 에코 메시지 수신
        str_len = read(sock, recv_buf, BUF_SIZE - 1);
        if (str_len <= 0)
        {
            if (str_len == 0)
            {
                fprintf(stderr, "[클라이언트 #%d] 서버가 연결을 종료함 (EOF)\n", client_id);
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    fprintf(stderr, "[클라이언트 #%d] read() timeout (EAGAIN/EWOULDBLOCK)\n", 
                            client_id);
                    continue;
                }
                else if (errno == EINTR)
                {
                    fprintf(stderr, "[클라이언트 #%d] read() interrupted (EINTR), 재시도\n", 
                            client_id);
                    continue;
                }
                else if (errno == ECONNRESET)
                {
                    fprintf(stderr, "[클라이언트 #%d] read() 실패: Connection reset (ECONNRESET)\n", 
                            client_id);
                }
                else
                {
                    fprintf(stderr, "[클라이언트 #%d] read() 실패: %s (errno: %d)\n", 
                            client_id, strerror(errno), errno);
                }
            }
            break;
        }
        
        recv_buf[str_len] = 0;
        
        count++;
    }
    
    end_time = time(NULL);
    
    printf("[클라이언트 #%d] 완료: %d I/O, %ld초 소요\n", 
           client_id, count, end_time - start_time);
    
    if (close(sock) == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] close() 실패: %s (errno: %d)\n", 
                client_id, strerror(errno), errno);
    }
}