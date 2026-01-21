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

#define POLL_TIMEOUT 10000

//  graceful shutdown 플래그 추가
static volatile sig_atomic_t client_running = 1;

//  SIGINT 핸들러 추가
static void
client_shutdown_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    
    // 플래그만 설정 (async-signal-safe)
    client_running = 0;
    
    // 간단한 메시지 (안전하게)
    const char msg[] = "\n[클라이언트] 종료 시그널 수신 (Ctrl+C)\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    
    errno = saved_errno;
}

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

    //  SIGINT 핸들러 설정
    struct sigaction sa_int;
    sa_int.sa_handler = client_shutdown_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;  // SA_RESTART 안 씀 (poll이 중단되도록)
    if (sigaction(SIGINT, &sa_int, NULL) == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGINT) 설정 실패: %s\n",
                client_id, strerror(errno));
        return;
    }

    // SIGPIPE 무시
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGPIPE) 설정 실패: %s\n",
                client_id, strerror(errno));
        return;
    }
    
    // 소켓 생성
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
    
    // 서버 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        fprintf(stderr, "[클라이언트 #%d] connect() 실패: %s\n", 
                client_id, strerror(errno));
        close(sock);
        return;
    }
    
    printf("[클라이언트 #%d] 서버 연결 성공! 10번 I/O 시작 (poll 타임아웃: %dms)...\n", 
           client_id, POLL_TIMEOUT);
    
    // poll 구조체 설정
    struct pollfd read_pfd = {
        .fd = sock,
        .events = POLLIN,
        .revents = 0
    };
    
    struct pollfd write_pfd = {
        .fd = sock,
        .events = POLLOUT,
        .revents = 0
    };

    //  메시지 송수신 - client_running 플래그 체크
    while (count < IO_COUNT && client_running)
    {
        //  루프 시작 시 종료 플래그 체크
        if (!client_running)
        {
            printf("[클라이언트 #%d] 사용자 요청으로 중단 (%d/%d I/O 완료)\n",
                   client_id, count, IO_COUNT);
            break;
        }
        
        // 메시지 생성
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", 
                 client_id, count + 1, time(NULL));
        
        // ===== 메시지 전송 =====
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        
        while (sent < msg_len && client_running)
        {
            write_pfd.revents = 0;
            int write_poll_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
            
            if (write_poll_ret == -1)
            {
                if (errno == EINTR)
                {
                    //  SIGINT로 인한 중단
                    if (!client_running)
                    {
                        printf("[클라이언트 #%d] 전송 중 중단됨\n", client_id);
                        goto cleanup;
                    }
                    printf("[클라이언트 #%d] write poll interrupted, 재시도\n", client_id);
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                fprintf(stderr, "[클라이언트 #%d] write poll() error: %s\n", 
                        client_id, strerror(errno));
                goto cleanup;
            }
            
            if (write_poll_ret == 0)
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 타임아웃\n", client_id);
                goto cleanup;
            }
            
            if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 에러 이벤트: ", client_id);
                if (write_pfd.revents & POLLERR)
                    fprintf(stderr, "POLLERR ");
                if (write_pfd.revents & POLLHUP)
                    fprintf(stderr, "POLLHUP ");
                if (write_pfd.revents & POLLNVAL)
                    fprintf(stderr, "POLLNVAL ");
                fprintf(stderr, "\n");
                goto cleanup;
            }
            
            if (write_pfd.revents & POLLOUT)
            {
                write_result = write(sock, msg + sent, msg_len - sent);
                
                if (write_result == -1)
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;
                    }
                    if (errno == EPIPE)
                    {
                        fprintf(stderr, "[클라이언트 #%d] write() EPIPE: 서버 연결 끊김\n", 
                                client_id);
                        goto cleanup;
                    }
                    fprintf(stderr, "[클라이언트 #%d] write() 실패: %s\n", 
                            client_id, strerror(errno));
                    goto cleanup;
                }
                sent += write_result;
            }
        }
        
        //  전송 완료 후 종료 플래그 체크
        if (!client_running)
        {
            printf("[클라이언트 #%d] 전송 완료 후 중단\n", client_id);
            break;
        }
        
        // ===== 읽기 =====
        read_pfd.revents = 0;
        int read_poll_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_poll_ret == -1)
        {
            if (errno == EINTR)
            {
                //  SIGINT로 인한 중단
                if (!client_running)
                {
                    printf("[클라이언트 #%d] 수신 대기 중 중단됨\n", client_id);
                    break;
                }
                printf("[클라이언트 #%d] read poll interrupted, 재시도\n", client_id);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            fprintf(stderr, "[클라이언트 #%d] read poll() error: %s\n", 
                    client_id, strerror(errno));
            goto cleanup;
        }
        
        if (read_poll_ret == 0)
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 타임아웃\n", client_id);
            goto cleanup;
        }
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 에러 이벤트: ", client_id);
            if (read_pfd.revents & POLLERR)
                fprintf(stderr, "POLLERR ");
            if (read_pfd.revents & POLLHUP)
                fprintf(stderr, "POLLHUP ");
            if (read_pfd.revents & POLLNVAL)
                fprintf(stderr, "POLLNVAL ");
            fprintf(stderr, "\n");
            goto cleanup;
        }
        
        if (read_pfd.revents & POLLIN)
        {
            str_len = read(sock, recv_buf, BUF_SIZE - 1);
            
            if (str_len > 0)
            {
                recv_buf[str_len] = 0;
                printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
            }
            else if (str_len == 0)
            {
                fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
                goto cleanup;
            }
            else
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                fprintf(stderr, "[클라이언트 #%d] read() 실패: %s\n", 
                        client_id, strerror(errno));
                goto cleanup;
            }
        }
        
        count++;
    }
    
cleanup:
    end_time = time(NULL);
    
    //  종료 이유 표시
    if (client_running)
    {
        printf("[클라이언트 #%d] 완료: %d I/O, %ld초 소요\n", 
               client_id, count, end_time - start_time);
    }
    else
    {
        printf("[클라이언트 #%d] 중단됨: %d/%d I/O 완료, %ld초 소요\n", 
               client_id, count, IO_COUNT, end_time - start_time);
    }
    
    close(sock);
}
