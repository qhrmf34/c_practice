#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include "mstHead.h"

void
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    printf("\n[자식 프로세스 #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[자식] 클라이언트: %s:%d\n", ip_str, ntohs(client_addr.sin_port));
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 1;
    monitor.total_sessions = 1;
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));              /* 세션 상태 동적 할당 */
    if (!session) 
    { 
        perror("malloc"); 
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
    struct pollfd read_pfd = {.fd = session->sock, .events = POLLIN, .revents = 0};
    while (session->io_count < IO_TARGET && session->state == SESSION_ACTIVE && state->running) 
    {
        time_t current_time = time(NULL);
        time_t idle_duration = current_time - session->last_activity;
        if (idle_duration >= SESSION_IDLE_TIMEOUT)                               /* 60초간 무활동 시 타임아웃 */
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] idle 타임아웃 (%ld초 무활동)\n", session_id, idle_duration);
            break;
        }
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);                         /* 1초 타임아웃으로 데이터 대기 */
        if (read_ret == -1)                                                      /* poll 실패 */
        {
            if (errno == EINTR)                                                  /* 시그널로 인한 중단 (SIGTERM 등) */
            {
                printf("child_process_main() : [자식 #%d] read poll interrupted, 재시도\n", session_id);
                continue;
            }
            fprintf(stderr, "child_process_main() : [자식 #%d] read poll() error: %s\n", session_id, strerror(errno));  /* 기타 에러: 잘못된 fd, 메모리 */
            break;
        }
        else if (read_ret == 0)                                                  /* 타임아웃 (데이터 없음) */
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] read poll 타임아웃\n", session_id);
            break;
        }
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))                   /* 소켓 에러 이벤트 */
        {
            log_message(state->log_ctx, LOG_ERROR, "child_process_main() : read poll 에러 이벤트 발생: 0x%x", read_pfd.revents);  /* POLLERR: 소켓 오류, POLLHUP: 연결 끊김 */
            break;
        }
        else if (read_pfd.revents & POLLIN)                                      /* 읽을 데이터 있음 */
        {
            ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);           /* 데이터 읽기 */
            if (str_len == 0)                                                    /* EOF: 클라이언트가 연결 정상 종료 */
            {
                printf("child_process_main() : [자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
                break;
            }
            else if (str_len < 0)                                                /* read 에러 */
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)                                                      /* 시그널로 인한 중단 (재시도 가능) */
                {
                    log_message(state->log_ctx, LOG_DEBUG, "child_process_main() : read() 시그널 중단, 재시도");
                    continue;
                }
                fprintf(stderr, "child_process_main() : [자식 #%d] read() error: %s\n", session_id, strerror(errno));  /* 기타: 연결 끊김, 소켓 오류 */
                break;
            }
            session->last_activity = time(NULL);                                 /* 활동 시간 업데이트 */
            buf[str_len] = 0;                                                    /* NULL 종료 문자 추가 */
            
            ssize_t sent = 0;
            while (sent < str_len) /* 에코: 읽은 데이터를 그대로 전송 */
            {
                ssize_t write_result = write(session->sock, buf + sent, str_len - sent);
                
                if (write_result == -1)                                          /* write 에러 */
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)  /* 재시도 가능한 에러 */
                    {
                        log_message(state->log_ctx, LOG_DEBUG, "child_process_main() : child_process_main() write error[%d:%s]", errno, strerror(errno));
                        continue;
                    }
                    else if (errno == EPIPE)                                     /* 클라이언트가 연결 끊음 (SIGPIPE 무시됨) */
                    {
                        fprintf(stderr, "child_process_main() : [자식 #%d] write() EPIPE: 클라이언트 연결 끊김\n", session_id);
                        break;
                    }
                    fprintf(stderr, "child_process_main() : [자식 #%d] write() error: %s\n", session_id, strerror(errno));
                    break;
                }
                sent += write_result;                                            /* 전송한 바이트 수 누적 */
            }
            session->io_count++;                                                 /* I/O 완료 카운트 증가 */
            session->last_activity = time(NULL);
            printf("[자식 #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
        }
        else 
        {
            log_message(state->log_ctx, LOG_DEBUG,"child_process_main() : read poll: 예상 못한 revents=0x%x", read_pfd.revents);
            break; 
        }
    }
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    if (!state->running)                                                         /* SIGTERM으로 종료된 경우 */
        printf("[자식 #%d (PID:%d)] SIGTERM으로 인한 graceful shutdown - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    else
        printf("[자식 #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    monitor.active_sessions--;
    if (close(client_sock) == -1)                                                /* 클라이언트 소켓 닫기 */
        fprintf(stderr, "[자식 #%d] close(client_sock) 실패: %s\n", session_id, strerror(errno));  /* close 실패: EIO 등 (드묾) */
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    free(session);                                                               /* 동적 할당 메모리 해제 */
    printf("[자식 #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}
