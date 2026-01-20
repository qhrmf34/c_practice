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

#define IO_TARGET 10         // 10번의 I/O 목표
#define POLL_TIMEOUT 1000   // poll 타임아웃 (밀리초) = 1초

// 자식 프로세스 메인 함수 - 스레드 없이 직접 처리
void 
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr)
{
    printf("\n[자식 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    // 리소스 모니터 초기화 (뮤텍스 불필요 - 단일 프로세스)
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 0;
    monitor.total_sessions = 0;
    
    // Session Descriptor 생성
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (session == NULL)
    {
        fprintf(stderr, "[자식 #%d] malloc() 실패: %s\n", 
                session_id, strerror(errno));
        close(client_sock);
        return;
    }
    // 세션 초기화
    memset(session, 0, sizeof(SessionDescriptor));
    session->sock = client_sock;
    session->addr = client_addr;
    session->session_id = session_id;
    session->state = SESSION_IDLE;
    
    // 모니터 업데이트
    monitor.active_sessions++;
    monitor.total_sessions++;
    
    // 초기 리소스 상태 출력
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    //  poll 구조체 설정 (읽기용)
    struct pollfd read_pfd;
    read_pfd.fd = session->sock;      // 감시할 소켓
    read_pfd.events = POLLIN;          // 읽기 가능 이벤트 감시
    
    //  poll 구조체 설정 (쓰기용)
    struct pollfd write_pfd;
    write_pfd.fd = session->sock;
    write_pfd.events = POLLOUT;

    // 메인 프로세스에서 처리
    char buf[BUF_SIZE];
    ssize_t str_len;
    ssize_t write_result;

    // 세션 활성화
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    // 클라이언트와 10번 I/O 수행
    while (session->io_count < IO_TARGET && 
           session->state == SESSION_ACTIVE &&
           !worker_shutdown_requested)  // SIGTERM 받으면 종료
    {
        // === 타임아웃 체크 ===
        time_t current_time = time(NULL);
        time_t session_duration = current_time - session->start_time;
        time_t idle_duration = current_time - session->last_activity;
        
        // 세션 전체 타임아웃 체크
        if (session_duration > SESSION_TIMEOUT)
        {
            fprintf(stderr, "[자식 #%d] 세션 타임아웃 (%ld초 초과)\n", 
                    session_id, session_duration);
            goto cleanup;
        }
        
        // idle 타임아웃 체크
        if (idle_duration > SESSION_IDLE_TIMEOUT)
        {
            fprintf(stderr, "[자식 #%d] idle 타임아웃 (%ld초 무활동)\n", 
                    session_id, idle_duration);
            goto cleanup;
        }
        
        // === 읽기: poll로 읽기 가능 여부 확인 (타임아웃 포함) ===
        int read_poll_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        if (read_poll_ret == -1)  //  read시 poll 에러
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("[자식 #%d] read poll interrupted (EINTR), 재시도\n", session_id);
                continue;
            }
            else  // 심각한 에러
            {
                fprintf(stderr, "[자식 #%d] read poll() error: %s\n", 
                        session_id, strerror(errno));
                goto cleanup;
            }
        }

        if (read_poll_ret == 0)  // 타임아웃 발생!
        {
            // poll timeout은 1초이므로, 다시 루프 돌아서 세션/idle timeout 체크
            continue;
        }
        
        // poll_ret > 0: 이벤트 발생
        // 에러 이벤트를 먼저 체크 
        if (read_pfd.revents & POLLNVAL)
        {
            fprintf(stderr, "[자식 #%d] [CRITICAL] read POLLNVAL: 잘못된 FD (%d)\n", 
                    session_id, session->sock);
            goto cleanup;
        }
        if (read_pfd.revents & POLLERR)
        {
            fprintf(stderr, "[자식 #%d] read POLLERR: 소켓 에러 발생\n", session_id);
            goto cleanup;
        }
        if (read_pfd.revents & POLLHUP)
        {
            printf("[자식 #%d] read POLLHUP: 클라이언트 연결 끊김\n", session_id);
            goto cleanup;
        }
        // revents 확인: POLLIN 이벤트
        if (read_pfd.revents & POLLIN)
        {
            // 데이터 읽기 (blocking, 하지만 poll에서 확인했으므로 즉시 리턴)
            str_len = read(session->sock, buf, BUF_SIZE - 1);
            if (str_len == 0)  // 클라이언트가 연결 종료 (EOF)
            {
                printf("[자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
                session->state = SESSION_CLOSING;
                goto cleanup;
            }
            // Read 에러
            if (str_len < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;  // 재시도 가능한 에러
                }
                
                fprintf(stderr, "[자식 #%d] read() error: %s\n", 
                        session_id, strerror(errno));
                goto cleanup;
            }

            session->last_activity = time(NULL);
            buf[str_len] = 0;  // NULL 종료 문자 추가
            
            // ===== Echo back: write 전에 POLLOUT 체크 =====
            ssize_t sent = 0;
            while (sent < str_len)
            {
                // write poll: 쓰기 가능 여부 확인
                int write_poll_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
                if (write_poll_ret == -1)  // poll 에러
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        printf("[자식 #%d] write poll interrupted (EINTR), 재시도\n", session_id);
                        continue;
                    }
                    // 심각한 에러
                    fprintf(stderr, "[자식 #%d] write poll() error: %s\n", 
                            session_id, strerror(errno));
                    goto cleanup;
                }
                if (write_poll_ret == 0)  // 타임아웃 발생!
                {
                    fprintf(stderr, "[자식 #%d] write poll 타임아웃 (%d초 초과, 쓰기 불가)\n", 
                            session_id, POLL_TIMEOUT / 1000);
                    goto cleanup;
                }
                
                // 에러 이벤트를 먼저 체크 
                if (write_pfd.revents & POLLNVAL)
                {
                    fprintf(stderr, "[자식 #%d] [CRITICAL] write POLLNVAL: 잘못된 FD (%d)\n", 
                            session_id, session->sock);
                    goto cleanup;
                }

                if (write_pfd.revents & POLLERR)
                {
                    fprintf(stderr, "[자식 #%d] write POLLERR: 소켓 에러 발생\n", session_id);
                    goto cleanup;
                }

                if (write_pfd.revents & POLLHUP)
                {
                    printf("[자식 #%d] write POLLHUP: 클라이언트 연결 끊김\n", session_id);
                    goto cleanup;
                }
                // POLLOUT 이벤트 확인: 쓰기 가능
                if (write_pfd.revents & POLLOUT)
                {
                    // write 수행 (blocking, 하지만 poll에서 확인했으므로 빠르게 리턴)
                    write_result = write(session->sock, buf + sent, str_len - sent);
                    
                    if (write_result == -1)  // write 에러
                    {
                        // EINTR, EAGAIN, EWOULDBLOCK은 재시도
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            continue;
                        }
                        // EPIPE: 클라이언트 연결 끊김 (SIGPIPE는 무시했으므로 EPIPE만 발생)
                        else if (errno == EPIPE)
                        {
                            fprintf(stderr, "[자식 #%d] write() EPIPE: 클라이언트 연결 끊김\n", 
                                    session_id);
                            goto cleanup;
                        }
                        else  
                        {
                            fprintf(stderr, "[자식 #%d] write() error: %s\n", 
                                    session_id, strerror(errno));
                            goto cleanup;
                        }
                    }
                    else  // write 성공
                    {
                        sent += write_result;
                    }
                }
            }
            // I/O 완료
            session->io_count++;
            session->last_activity = time(NULL);  // 활동 시간 업데이트
            printf("[자식 #%d] I/O 완료: %d/%d\n", 
                   session_id, session->io_count, IO_TARGET);
        }
    }
    
cleanup:
    // 세션 종료 처리 
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    // 종료 이유 로그
    if (worker_shutdown_requested)
    {
        printf("[자식 #%d (PID:%d)] SIGTERM으로 인한 graceful shutdown - %d I/O 완료, %ld초 소요\n", 
               session_id, getpid(),
               session->io_count, end_time - session->start_time);
    }
    else
    {
        printf("[자식 #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", 
               session_id, getpid(),
               session->io_count, end_time - session->start_time);
    }
    
    // 모니터 업데이트
    monitor.active_sessions--;
    
    // 소켓 닫기
    if (close(client_sock) == -1)
    {
        fprintf(stderr, "[자식 #%d] close(client_sock) 실패: %s\n", 
                session_id, strerror(errno));
    }
    
    // 최종 리소스 상태 출력
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    // 세션 정리
    free(session);
    
    printf("[자식 #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}