#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#define IO_TARGET 10  // 10번의 I/O 목표

// 전역 리소스 모니터 (자식 프로세스 내)
static ResourceMonitor g_monitor = {0};
static pthread_mutex_t g_monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

// 클라이언트 처리 스레드
void*
handle_client_thread(void *arg)
{
    SessionDescriptor *session = (SessionDescriptor *)arg;
    char buf[BUF_SIZE];
    ssize_t str_len;
    ssize_t write_result;
    
    printf("  [스레드 #%d (TID:%lu)] 시작\n", 
           session->session_id, (unsigned long)pthread_self());
    
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    // 클라이언트와 10번 I/O 수행
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE)
    {
        // 데이터 읽기 (blocking)
        str_len = read(session->sock, buf, BUF_SIZE - 1);
        
        if (str_len > 0)
        {
            session->last_activity = time(NULL);
            buf[str_len] = 0;
            
            // Echo back (blocking)
            ssize_t sent = 0;
            while (sent < str_len)
            {
                write_result = write(session->sock, buf + sent, str_len - sent);
                
                if (write_result == -1)
                {
                    // EINTR, EAGAIN, EWOULDBLOCK은 재시도
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;
                    }
                    else
                    {
                        fprintf(stderr, "  [스레드 #%d] write() error: %s\n", 
                                session->session_id, strerror(errno));
                        session->state = SESSION_CLOSING;
                        goto cleanup;
                    }
                }
                else
                {
                    sent += write_result;
                }
            }
            
            session->io_count++;
            printf("  [스레드 #%d] I/O 완료: %d/%d\n", 
                   session->session_id, session->io_count, IO_TARGET);
        }
        else if (str_len == 0)
        {
            printf("  [스레드 #%d] 클라이언트 정상 연결 종료 (EOF)\n", 
                   session->session_id);
            session->state = SESSION_CLOSING;
            break;
        }
        else
        {
            // EINTR, EAGAIN, EWOULDBLOCK은 재시도
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            else
            {
                fprintf(stderr, "  [스레드 #%d] read() error: %s\n", 
                        session->session_id, strerror(errno));
                session->state = SESSION_CLOSING;
                break;
            }
        }
    }

cleanup:
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    
    printf("  [스레드 #%d (TID:%lu)] 종료 - %d I/O 완료, %ld초 소요\n", 
           session->session_id, (unsigned long)pthread_self(),
           session->io_count, end_time - session->start_time);
    
    // 모니터 업데이트
    pthread_mutex_lock(&g_monitor_mutex);
    g_monitor.active_sessions--;
    pthread_mutex_unlock(&g_monitor_mutex);
    
    return NULL;
}

// 자식 프로세스 메인 함수
void
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr)
{
    signal(SIGPIPE, SIG_IGN);
    
    printf("\n[자식 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    // 리소스 모니터 초기화
    g_monitor.start_time = time(NULL);
    g_monitor.active_sessions = 0;
    g_monitor.total_sessions = 0;
    
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
    pthread_mutex_lock(&g_monitor_mutex);
    g_monitor.active_sessions++;
    g_monitor.total_sessions++;
    pthread_mutex_unlock(&g_monitor_mutex);
    
    // 초기 리소스 상태 출력
    monitor_resources(&g_monitor);
    print_resource_status(&g_monitor);
    
    // 스레드 생성
    int ret = pthread_create(&session->thread_id, NULL, handle_client_thread, session);
    if (ret != 0)
    {
        fprintf(stderr, "[자식 #%d] pthread_create() 실패: %s\n", 
                session_id, strerror(ret));
        
        pthread_mutex_lock(&g_monitor_mutex);
        g_monitor.active_sessions--;
        pthread_mutex_unlock(&g_monitor_mutex);
        
        free(session);
        close(client_sock);
        return;
    }
    
    printf("[자식 #%d] 스레드 생성 성공 (TID: %lu)\n", 
           session_id, (unsigned long)session->thread_id);
    
    // 스레드 종료 대기
    ret = pthread_join(session->thread_id, NULL);
    if (ret != 0)
    {
        fprintf(stderr, "[자식 #%d] pthread_join() 실패: %s\n", 
                session_id, strerror(ret));
    }
    
    // 소켓 닫기
    if (close(client_sock) == -1)
    {
        fprintf(stderr, "[자식 #%d] close(client_sock) 실패: %s\n", 
                session_id, strerror(errno));
    }
    
    // 최종 리소스 상태 출력
    monitor_resources(&g_monitor);
    print_resource_status(&g_monitor);
    
    // 세션 정리
    free(session);
    
    printf("[자식 #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}