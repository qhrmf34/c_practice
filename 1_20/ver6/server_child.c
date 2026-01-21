#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

/*
 * Worker 메인 함수 (자식 프로세스)
 * 
 * @param client_sock: 클라이언트 소켓 (FD=3)
 * @param session_id: 세션 ID
 * @param client_addr: 클라이언트 주소
 * @param ctx: Worker 컨텍스트 (running flag 포함)
 * @param log_ctx: 로그 컨텍스트
 * 
 * 동작:
 * 1. 10번 I/O 수행 (echo)
 * 2. SIGTERM 수신 시 ctx->running=0 → graceful shutdown
 * 3. idle 타임아웃 체크
 * 4. goto cleanup만 사용 (break 없음)
 */
void
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, WorkerContext *ctx, LogContext *log_ctx)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    
    printf("\n[Worker #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[Worker] 클라이언트: %s:%d\n", ip_str, ntohs(client_addr.sin_port));
    
    // 리소스 모니터 초기화
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 1;
    monitor.total_sessions = 1;
    
    // 세션 디스크립터 생성
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (session == NULL) 
    {
        fprintf(stderr, "[Worker #%d] malloc() 실패\n", session_id);
        close(client_sock);
        return;
    }
    
    memset(session, 0, sizeof(SessionDescriptor));
    session->sock = client_sock;
    session->addr = client_addr;
    session->session_id = session_id;
    session->state = SESSION_ACTIVE;
    session->start_time = time(NULL);
    session->last_activity = time(NULL);
    session->io_count = 0;
    
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    
    char buf[BUF_SIZE];
    struct pollfd read_pfd = {.fd = session->sock, .events = POLLIN};
    struct pollfd write_pfd = {.fd = session->sock, .events = POLLOUT};
    
    // 메인 루프: 10번 I/O 또는 SIGTERM까지
    while (session->io_count < IO_TARGET && ctx->running) 
    {
        // [1] idle 타임아웃 체크
        time_t idle_duration = time(NULL) - session->last_activity;
        if (idle_duration >= SESSION_IDLE_TIMEOUT)
         {
            fprintf(stderr, "[Worker #%d] idle 타임아웃 (%ld초)\n", session_id, idle_duration);
            goto cleanup;
        }
        
        // [2] 읽기 poll
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_ret == -1) 
        {
            if (errno == EINTR) continue;  // 시그널 인터럽트 → 재시도
            fprintf(stderr, "[Worker #%d] read poll 에러: %s\n", session_id, strerror(errno));
            goto cleanup;
        }
        else if (read_ret == 0) 
            continue;  // 타임아웃 → 다시 루프
        // 에러 이벤트 → cleanup
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            goto cleanup;
        // [3] 데이터 읽기
        else if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);
            
            if (str_len == 0) 
            {  // EOF → 클라이언트 정상 종료
                printf("[Worker #%d] 클라이언트 연결 종료 (EOF)\n", session_id);
                goto cleanup;
            }
            else if (str_len < 0) 
            {
                if (errno == EINTR || errno == EAGAIN) continue;
                fprintf(stderr, "[Worker #%d] read() 에러: %s\n", session_id, strerror(errno));
                goto cleanup;
            }
            
            session->last_activity = time(NULL);
            buf[str_len] = 0;
            
            // [4] Echo back (쓰기 poll)
            ssize_t sent = 0;
            while (sent < str_len) 
            {
                write_pfd.revents = 0;
                int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
                
                if (write_ret == -1) 
                {
                    if (errno == EINTR) continue;
                    fprintf(stderr, "[Worker #%d] write poll 에러\n", session_id);
                    goto cleanup;
                }
                else if (write_ret == 0) 
                    continue;  // 타임아웃
                
                if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                    goto cleanup;
                else if (write_pfd.revents & POLLOUT) 
                {
                    ssize_t n = write(session->sock, buf + sent, str_len - sent);
                    
                    if (n == -1) 
                    {
                        if (errno == EINTR || errno == EAGAIN) continue;
                        if (errno == EPIPE) {  // 클라이언트 연결 끊김
                            fprintf(stderr, "[Worker #%d] EPIPE: 클라이언트 끊김\n", session_id);
                            goto cleanup;
                        }
                        fprintf(stderr, "[Worker #%d] write() 에러\n", session_id);
                        goto cleanup;
                    }
                    sent += n;
                }
            }
            
            // I/O 완료
            session->io_count++;
            session->last_activity = time(NULL);
            printf("[Worker #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
        }
    }

cleanup:
    time_t end_time = time(NULL);
    
    // 종료 이유 출력
    if (!ctx->running)
        printf("[Worker #%d] SIGTERM으로 종료 - %d I/O, %ld초\n", 
               session_id, session->io_count, end_time - session->start_time);
    else
        printf("[Worker #%d] 처리 완료 - %d I/O, %ld초\n", 
               session_id, session->io_count, end_time - session->start_time);
    
    monitor.active_sessions--;
    close(client_sock);
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    free(session);
    
    printf("[Worker #%d] 종료\n\n", session_id);
}
