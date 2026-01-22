#include "server_function.h"

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
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (!session) 
    {
        perror("child_process_main() : malloc");
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
        if (idle_duration >= SESSION_IDLE_TIMEOUT) 
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] idle 타임아웃 (%ld초 무활동)\n", session_id, idle_duration);
            break;
        }
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        if (read_ret == -1) 
        {
            if (errno == EINTR) 
            {
                printf("child_process_main() : [자식 #%d] read poll interrupted, 재시도\n", session_id);
                continue;
            }
            fprintf(stderr, "child_process_main() : [자식 #%d] poll() error: %s\n", session_id, strerror(errno));
            break;
        } 
        else if (read_ret == 0) 
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] poll 타임아웃\n", session_id);
            break;
        }
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] poll 에러 이벤트: 0x%x\n", session_id, read_pfd.revents);
            break;
        } 
        else if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);
            if (str_len == 0) 
            {
                printf("child_process_main() : [자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
                break;
            } 
            else if (str_len < 0) 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
                {
                    printf("child_process_main() : [자식 #%d] read() 재시도 가능 에러\n", session_id);
                    continue;
                }
                fprintf(stderr, "child_process_main() : [자식 #%d] read() error: %s\n", session_id, strerror(errno));
                break;
            }
            session->last_activity = time(NULL);
            buf[str_len] = 0;
            ssize_t sent = 0;
            while (sent < str_len) 
            {
                ssize_t write_result = write(session->sock, buf + sent, str_len - sent);
                if (write_result == -1) 
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
                        continue;
                    else if (errno == EPIPE) 
                    {
                        fprintf(stderr, "child_process_main() : [자식 #%d] write() EPIPE: 클라이언트 연결 끊김\n", session_id);
                        break;
                    }
                    fprintf(stderr, "child_process_main() : [자식 #%d] write() error: %s\n", session_id, strerror(errno));
                    break;
                }
                sent += write_result;
            }
            if (sent < str_len)
                break;
            session->io_count++;
            session->last_activity = time(NULL);
            printf("[자식 #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
        } 
        else 
        {
            fprintf(stderr, "child_process_main() : [자식 #%d] 예상 못한 revents=0x%x\n", session_id, read_pfd.revents);
            break;
        }
    }
    session->state = SESSION_CLOSED;
    time_t end_time = time(NULL);
    if (!state->running)
        printf("[자식 #%d (PID:%d)] SIGTERM으로 인한 graceful shutdown - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    else
        printf("[자식 #%d (PID:%d)] 처리 완료 - %d I/O 완료, %ld초 소요\n", session_id, getpid(), session->io_count, end_time - session->start_time);
    monitor.active_sessions--;
    if (close(client_sock) == -1)
        fprintf(stderr, "child_process_main() : [자식 #%d] close(client_sock) 실패: %s\n", session_id, strerror(errno));
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    free(session);
    printf("[자식 #%d (PID:%d)] 정상 종료\n\n", session_id, getpid());
}
