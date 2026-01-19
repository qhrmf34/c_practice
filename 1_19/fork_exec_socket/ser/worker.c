#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <arpa/inet.h>

#define IO_TARGET 10         // 10번의 I/O 목표
#define POLL_TIMEOUT 1000   // poll 타임아웃 (밀리초) = 1초

// Worker 프로세스 메인 함수
void 
worker_process_main(int client_sock, int session_id)
{
    printf("\n[Worker 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    
    // 리소스 모니터 초기화
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 0;
    monitor.total_sessions = 0;
    
    // Session Descriptor 생성
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (session == NULL)
    {
        fprintf(stderr, "[Worker #%d] malloc() 실패: %s\n", 
                session_id, strerror(errno));
        close(client_sock);
        return;
    }
    
    // 세션 초기화
    memset(session, 0, sizeof(SessionDescriptor));
    session->sock = client_sock;
    session->session_id = session_id;
    session->state = SESSION_IDLE;
    
    // 클라이언트 주소 정보 가져오기 (optional - getpeername 사용)
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len) == 0)
    {
        session->addr = client_addr;
        printf("[Worker] 클라이언트: %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }
    else
    {
        printf("[Worker] 클라이언트 주소 정보 가져오기 실패: %s\n", strerror(errno));
    }
    
    // 모니터 업데이트
    monitor.active_sessions++;
    monitor.total_sessions++;
    
    // 초기 리소스 상태 출력
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    // poll 구조체 설정 (읽기용)
    struct pollfd read_pfd;
    read_pfd.fd = session->sock;
    read_pfd.events = POLLIN;
    
    // poll 구조체 설정 (쓰기용)
    struct pollfd write_pfd;
    write_pfd.fd = session->sock;
    write_pfd.events = POLLOUT;

    char buf[BUF_SIZE];
    ssize_t str_len;
    ssize_t write_result;

    // 세션 활성화
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    // 클라이언트와 10번 I/O 수행
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE)
    {
        // 읽기: poll로 읽기 가능 여부 확인 (타임아웃 포함)
        int read_poll_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        if (read_poll_ret == -1)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("[Worker #%d] read poll interrupted (EINTR), 재시도\n", session_id);
                continue;
            }
            else
            {
                fprintf(stderr, "[Worker #%d] read poll() error: %s\n", 
                        session_id, strerror(errno));
                goto cleanup;
            }
        }

        if (read_poll_ret == 0)
        {
            fprintf(stderr, "[Worker #%d] read poll 타임아웃 (%d초 초과, 데이터 수신 없음)\n", 
                    session_id, POLL_TIMEOUT / 1000);
            goto cleanup;
        }
        
        // 에러 이벤트 체크
        if (read_pfd.revents & POLLNVAL)
        {
            fprintf(stderr, "[Worker #%d] [CRITICAL] read POLLNVAL: 잘못된 FD (%d)\n", 
                    session_id, session->sock);
            goto cleanup;
        }
        if (read_pfd.revents & POLLERR)
        {
            fprintf(stderr, "[Worker #%d] read POLLERR: 소켓 에러 발생\n", session_id);
            goto cleanup;
        }
        if (read_pfd.revents & POLLHUP)
        {
            printf("[Worker #%d] read POLLHUP: 클라이언트 연결 끊김\n", session_id);
            goto cleanup;
        }
        
        // POLLIN 이벤트 확인
        if (read_pfd.revents & POLLIN)
        {
            str_len = read(session->sock, buf, BUF_SIZE - 1);
            if (str_len == 0)
            {
                printf("[Worker #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
                session->state = SESSION_CLOSING;
                goto cleanup;
            }
            
            if (str_len < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                
                fprintf(stderr, "[Worker #%d] read() error: %s\n", 
                        session_id, strerror(errno));
                goto cleanup;
            }

            session->last_activity = time(NULL);
            buf[str_len] = 0;
            
            // Echo back: write 전에 POLLOUT 체크
            ssize_t sent = 0;
            while (sent < str_len)
            {
                int write_poll_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
                if (write_poll_ret == -1)
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        printf("[Worker #%d] write poll interrupted (EINTR), 재시도\n", session_id);
                        continue;
                    }
                    fprintf(stderr, "[Worker #%d] write poll() error: %s\n", 
                            session_id, strerror(errno));
                    goto cleanup;
                }
                if (write_poll_ret == 0)
                {
                    fprintf(stderr, "[Worker #%d] write poll 타임아웃 (%d초 초과, 쓰기 불가)\n", 
                            session_id, POLL_TIMEOUT / 1000);
                    goto cleanup;
                }
                
                // 에러 이벤트 체크
                if (write_pfd.revents & POLLNVAL)
                {
                    fprintf(stderr, "[Worker #%d] [CRITICAL] write POLLNVAL: 잘못된 FD (%d)\n", 
                            session_id, session->sock);
                    goto cleanup;
                }
                if (write_pfd.revents & POLLERR)
                {
                    fprintf(stderr, "[Worker #%d] write POLLERR: 소켓 에러 발생\n", session_id);
                    goto cleanup;
                }
                if (write_pfd.revents & POLLHUP)
                {
                    printf("[Worker #%d] write POLLHUP: 클라이언트 연결 끊김\n", session_id);
                    goto cleanup;
                }
                
                // POLLOUT 이벤트 확인
                if (write_pfd.revents & POLLOUT)
                {
                    write_result = write(session->sock, buf + sent, str_len - sent);
                    
                    if (write_result == -1)
                    {
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            continue;
                        }
                        else
                        {
                            fprintf(stderr, "[Worker #%d] write() error: %s\n", 
                                    session_id, strerror(errno));
                            goto cleanup;
                        }
                    }
                    else
                    {
                        sent += write_result;
                    }
                }
            }
            
            // I/O 완료
            session->io_count++;
            printf("[Worker #%d] I/O 완료: %d/%d\n", 
                   session_id, session->io_count, IO_TARGET);
        }
    }

cleanup:
    // 세션 종료 처리
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    printf("[Worker #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", 
           session_id, getpid(),
           session->io_count, end_time - session->start_time);
    
    // 모니터 업데이트
    monitor.active_sessions--;
    
    // 소켓 닫기
    if (close(client_sock) == -1)
    {
        fprintf(stderr, "[Worker #%d] close(client_sock) 실패: %s\n", 
                session_id, strerror(errno));
    }
    
    // 최종 리소스 상태 출력
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    // 세션 정리
    free(session);
    
    printf("[Worker #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}