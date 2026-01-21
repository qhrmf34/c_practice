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

/* 시그널 핸들러는 async-signal-safe 제약으로 전역 변수 필요 */
static ClientState *g_client_state = NULL;

static void
signal_handler(int signo)
{
    int saved_errno = errno;                                                     /* errno 보존 (시그널 핸들러 필수) */
    
    if (signo == SIGINT)                                                         /* Ctrl+C 시그널 */
    {
        if (g_client_state)
            g_client_state->running = 0;                                         /* 종료 플래그 설정 */
        const char msg[] = "\n[클라이언트] 종료 시그널 수신\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);                              /* async-signal-safe 출력 */
    }
    
    errno = saved_errno;                                                         /* errno 복원 */
}

void
client_run(const char *ip, int port, int client_id, ClientState *state)
{
    int sock, count = 0;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE], recv_buf[BUF_SIZE];
    time_t start_time, end_time;
    
    g_client_state = state;                                                      /* 시그널 핸들러용 전역 설정 */
    start_time = time(NULL);
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)                                      /* SIGINT 핸들러 등록 실패: 시스템 제한 */
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGINT) 실패\n", client_id);
        return;
    }
    
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1)                                     /* SIGPIPE 무시 (끊긴 소켓 write 방어) */
    {
        fprintf(stderr, "[클라이언트 #%d] sigaction(SIGPIPE) 실패\n", client_id);
        return;
    }
    
    sock = socket(PF_INET, SOCK_STREAM, 0);                                      /* TCP 소켓 생성 */
    if (sock == -1)                                                              /* socket 실패: fd 한계, 권한, 메모리 */
    {
        fprintf(stderr, "[클라이언트 #%d] socket() 실패: %s\n", client_id, strerror(errno));
        return;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0)                        /* IP 문자열을 네트워크 주소로 변환 */
    {
        fprintf(stderr, "[클라이언트 #%d] 잘못된 IP 주소: %s\n", client_id, ip);  /* 변환 실패: 잘못된 IP 형식 */
        close(sock);
        return;
    }
    serv_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)    /* 서버에 연결 */
    {
        fprintf(stderr, "[클라이언트 #%d] connect() 실패: %s\n", client_id, strerror(errno));  /* 연결 실패: 서버 없음, 네트워크 에러, 방화벽 */
        close(sock);
        return;
    }
    
    printf("[클라이언트 #%d] 서버 연결 성공!\n", client_id);
    
    struct pollfd write_pfd = {.fd = sock, .events = POLLOUT, .revents = 0};
    struct pollfd read_pfd = {.fd = sock, .events = POLLIN, .revents = 0};
    
    while (count < IO_COUNT && state->running) 
    {
        if (!state->running)                                                     /* 종료 신호 확인 */
        {
            printf("[클라이언트 #%d] 중단 (%d/%d 완료)\n", client_id, count, IO_COUNT);
            goto cleanup;
        }
        
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
        
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        
        /* 메시지 전송 */
        while (sent < msg_len && state->running) 
        {
            write_pfd.revents = 0;
            int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);                   /* 쓰기 가능 대기 (10초 타임아웃) */
            
            if (write_ret == -1)                                                 /* poll 실패 */
            {
                if (errno == EINTR)                                              /* 시그널로 중단 (SIGINT) */
                {
                    if (!state->running) 
                    {
                        printf("[클라이언트 #%d] 전송 중 중단\n", client_id);
                        goto cleanup;
                    }
                    continue;
                }
                fprintf(stderr, "[클라이언트 #%d] write poll 에러\n", client_id);  /* 기타 에러: 잘못된 fd, 메모리 */
                goto cleanup;
            }
            else if (write_ret == 0)                                             /* 타임아웃 (10초 내 쓰기 불가) */
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 타임아웃\n", client_id);
                goto cleanup;
            }
            
            if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))              /* 소켓 에러 */
            {
                fprintf(stderr, "[클라이언트 #%d] write poll 에러 이벤트\n", client_id);  /* POLLERR: 소켓 오류, POLLHUP: 연결 끊김 */
                goto cleanup;
            }
            else if (write_pfd.revents & POLLOUT)                                /* 쓰기 가능 */
            {
                ssize_t write_result = write(sock, msg + sent, msg_len - sent);
                
                if (write_result == -1)                                          /* write 에러 */
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)  /* 재시도 가능 */
                        continue;
                    if (errno == EPIPE)                                          /* 서버가 연결 끊음 */
                    {
                        fprintf(stderr, "[클라이언트 #%d] EPIPE: 서버 연결 끊김\n", client_id);
                        goto cleanup;
                    }
                    fprintf(stderr, "[클라이언트 #%d] write() 실패\n", client_id);  /* 기타: 네트워크 에러 */
                    goto cleanup;
                }
                sent += write_result;                                            /* 전송한 바이트 수 누적 */
            }
        }
        
        if (!state->running)                                                     /* 전송 완료 후 종료 확인 */
        {
            printf("[클라이언트 #%d] 전송 완료 후 중단\n", client_id);
            goto cleanup;
        }
        
        /* 에코 응답 수신 */
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);                         /* 읽기 대기 (10초 타임아웃) */
        
        if (read_ret == -1)                                                      /* poll 실패 */
        {
            if (errno == EINTR)                                                  /* 시그널로 중단 */
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
        else if (read_ret == 0)                                                  /* 타임아웃 */
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 타임아웃\n", client_id);
            goto cleanup;
        }
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))                   /* 소켓 에러 */
        {
            fprintf(stderr, "[클라이언트 #%d] read poll 에러 이벤트\n", client_id);
            goto cleanup;
        }
        else if (read_pfd.revents & POLLIN)                                      /* 읽을 데이터 있음 */
        {
            ssize_t str_len = read(sock, recv_buf, BUF_SIZE - 1);
            
            if (str_len > 0)                                                     /* 데이터 수신 성공 */
            {
                recv_buf[str_len] = 0;                                           /* NULL 종료 문자 */
                printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
            } 
            else if (str_len == 0)                                               /* EOF: 서버가 연결 종료 */
            {
                fprintf(stderr, "[클라이언트 #%d] 서버 연결 종료\n", client_id);
                goto cleanup;
            } 
            else                                                                 /* read 에러 */
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)   /* 재시도 가능 */
                    continue;
                fprintf(stderr, "[클라이언트 #%d] read() 실패\n", client_id);
                goto cleanup;
            }
        }
        count++;                                                                 /* I/O 완료 카운트 증가 */
    }

cleanup:
    end_time = time(NULL);
    
    if (state->running)
        printf("[클라이언트 #%d] 완료: %d I/O, %ld초\n", client_id, count, end_time - start_time);
    else
        printf("[클라이언트 #%d] 중단: %d/%d I/O, %ld초\n", client_id, count, IO_COUNT, end_time - start_time);
    
    close(sock);                                                                 /* 소켓 닫기 */
}
