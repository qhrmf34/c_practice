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

#define POLL_TIMEOUT 30000   // poll 타임아웃 (밀리초) = 30초

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
    
    printf("[클라이언트 #%d] 서버 연결 성공! 10번 I/O 시작 (poll 타임아웃: %dms)...\n", 
           client_id, POLL_TIMEOUT);
    
    //  poll 구조체 설정 
    struct pollfd pfd;
    pfd.fd = sock;           // 감시할 소켓
    pfd.events = POLLIN;     // 읽기 가능 이벤트 감시
    
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
        
        //  poll로 읽기 가능 여부 확인 (타임아웃 포함) 
        int poll_ret = poll(&pfd, 1, POLL_TIMEOUT);
        
        if (poll_ret == -1)  // poll 에러
        {
            if (errno == EINTR)  // 시그널로 인터럽트 (재시도)
            {
                printf("[클라이언트 #%d] poll interrupted (EINTR), 재시도\n", client_id);
                continue;
            }
            else  // 심각한 에러
            {
                fprintf(stderr, "[클라이언트 #%d] poll() error: %s\n", 
                        client_id, strerror(errno));
                close(sock);
                return;
            }
        }
        else if (poll_ret == 0)  //  타임아웃 발생! 
        {
            fprintf(stderr, "[클라이언트 #%d] poll 타임아웃 (%d초 초과, 서버 응답 없음)\n", 
                    client_id, POLL_TIMEOUT / 1000);
            close(sock);
            return;
        }
        else  // poll_ret > 0: 이벤트 발생
        {
            // revents 확인: 어떤 이벤트가 발생했는지
            if (pfd.revents & POLLIN)  // 읽기 가능
            {
                // 에코 메시지 수신 (blocking, 하지만 poll에서 확인했으므로 즉시 리턴)
                str_len = read(sock, recv_buf, BUF_SIZE - 1);
                
                if (str_len > 0)  // 데이터 수신 성공
                {
                    recv_buf[str_len] = 0;
                    printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
                }
                else if (str_len == 0)  // 서버가 연결 종료 (EOF)
                {
                    fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
                    close(sock);
                    return;
                }
                else  // str_len == -1, read 에러
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
            }
            else if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))  // 에러 이벤트
            {
                fprintf(stderr, "[클라이언트 #%d] poll 에러 이벤트: ", client_id);
                
                if (pfd.revents & POLLERR) //비동기 에러
                    fprintf(stderr, "POLLERR ");
                if (pfd.revents & POLLHUP) //상대 프로세스가 소켓 닫음
                    fprintf(stderr, "POLLHUP ");
                if (pfd.revents & POLLNVAL) //fd가 유효하지 않음
                    fprintf(stderr, "POLLNVAL ");
                
                fprintf(stderr, "\n");
                close(sock);
                return;
            }
        }
        
        count++;
    }
    
    end_time = time(NULL);
    
    printf("[클라이언트 #%d] 완료: %d I/O, %ld초 소요\n", 
           client_id, count, end_time - start_time);
    
    close(sock);
}