#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>

static volatile sig_atomic_t worker_running = 1;

static void
worker_signal_handler(int signo)
{
    (void)signo;
    worker_running = 0;
}

void
child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr)
{
    struct sigaction sa;
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    
    printf("\n[Worker #%d (PID:%d)] 시작\n", session_id, getpid());
    printf("[Worker] 클라이언트: %s:%d\n", ip_str, ntohs(client_addr.sin_port));
    
    ResourceMonitor monitor = {0};
    monitor.start_time = time(NULL);
    monitor.active_sessions = 1;
    monitor.total_sessions = 1;
    
    SessionDescriptor *session = malloc(sizeof(SessionDescriptor));
    if (session == NULL) {
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
    struct pollfd read_pfd = {.fd = session->sock, .events = POLLIN, .revents = 0};
    struct pollfd write_pfd = {.fd = session->sock, .events = POLLOUT, .revents = 0};
    
    while (session->io_count < IO_TARGET && worker_running) {
        time_t idle_duration = time(NULL) - session->last_activity;
        if (idle_duration >= SESSION_IDLE_TIMEOUT) {
            fprintf(stderr, "[Worker #%d] idle 타임아웃\n", session_id);
            goto cleanup;
        }
        
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        
        if (read_ret == -1) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[Worker #%d] read poll 에러\n", session_id);
            goto cleanup;
        }
        
        if (read_ret == 0)
            continue;
        
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            goto cleanup;
        
        if (read_pfd.revents & POLLIN) {
            ssize_t str_len = read(session->sock, buf, BUF_SIZE - 1);
            
            if (str_len == 0) {
                printf("[Worker #%d] 클라이언트 연결 종료\n", session_id);
                goto cleanup;
            }
            
            if (str_len < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                goto cleanup;
            }
            
            session->last_activity = time(NULL);
            buf[str_len] = 0;
            
            ssize_t sent = 0;
            while (sent < str_len) {
                write_pfd.revents = 0;
                int write_ret = poll(&write_pfd, 1, POLL_TIMEOUT);
                
                if (write_ret == -1) {
                    if (errno == EINTR)
                        continue;
                    goto cleanup;
                }
                
                if (write_ret == 0)
                    continue;
                
                if (write_pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                    goto cleanup;
                
                if (write_pfd.revents & POLLOUT) {
                    ssize_t write_result = write(session->sock, buf + sent, str_len - sent);
                    
                    if (write_result == -1) {
                        if (errno == EINTR || errno == EAGAIN)
                            continue;
                        if (errno == EPIPE)
                            goto cleanup;
                        goto cleanup;
                    }
                    sent += write_result;
                }
            }
            
            session->io_count++;
            session->last_activity = time(NULL);
            printf("[Worker #%d] I/O 완료: %d/%d\n", session_id, session->io_count, IO_TARGET);
        }
    }

cleanup:
    time_t end_time = time(NULL);
    
    if (!worker_running)
        printf("[Worker #%d] SIGTERM으로 종료 - %d I/O, %ld초\n", session_id, session->io_count, end_time - session->start_time);
    else
        printf("[Worker #%d] 처리 완료 - %d I/O, %ld초\n", session_id, session->io_count, end_time - session->start_time);
    
    monitor.active_sessions--;
    close(client_sock);
    monitor_resources(&monitor);
    print_resource_status(&monitor);
    free(session);
    
    printf("[Worker #%d] 종료\n\n", session_id);
}
