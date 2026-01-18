#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define IO_TARGET 10  // 10번의 I/O 목표

// 자식 프로세스 메인 함수 - 스레드 없이 직접 처리
void child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr)
{
    // SIGPIPE 무시
    signal(SIGPIPE, SIG_IGN);
    
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
    
    // 메인 스레드 에서 처리
    
    char buf[BUF_SIZE];
    ssize_t str_len;
    ssize_t write_result;
    
    printf("[자식 #%d] 클라이언트 처리 시작 (PID:%d)\n", 
           session_id, getpid());
    
    // 세션 활성화
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    // 클라이언트와 10번 I/O 수행
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE)
    {
        // 데이터 읽기 (blocking)
        str_len = read(session->sock, buf, BUF_SIZE - 1);
        
        if (str_len > 0)  // 데이터 수신 성공
        {
            session->last_activity = time(NULL);
            buf[str_len] = 0;  // NULL 종료 문자 추가
            
            // Echo back (blocking) - partial write 처리
            ssize_t sent = 0;
            while (sent < str_len)
            {
                write_result = write(session->sock, buf + sent, str_len - sent);
                
                if (write_result == -1)  // write 에러
                {
                    // EINTR, EAGAIN, EWOULDBLOCK은 재시도
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;
                    }
                    else  
                    {
                        fprintf(stderr, "[자식 #%d] write() error: %s\n", 
                                session_id, strerror(errno));
                        session->state = SESSION_CLOSING;
                        goto cleanup;
                    }
                }
                else  // write 성공
                {
                    sent += write_result;
                }
            }
            
            // I/O 완료
            session->io_count++;
            printf("[자식 #%d] I/O 완료: %d/%d\n", 
                   session_id, session->io_count, IO_TARGET);
        }
        else if (str_len == 0)  // 클라이언트가 연결 종료 (EOF)
        {
            printf("[자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
            session->state = SESSION_CLOSING;
            break;
        }
        else  // str_len == -1, read 에러
        {
            // EINTR, EAGAIN, EWOULDBLOCK은 재시도
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            else  // 심각한 에러
            {
                fprintf(stderr, "[자식 #%d] read() error: %s\n", 
                        session_id, strerror(errno));
                session->state = SESSION_CLOSING;
                break;
            }
        }
    }

cleanup:
    // 세션 종료 처리
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    printf("[자식 #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", 
           session_id, getpid(),
           session->io_count, end_time - session->start_time);
    
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