#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define IDLE_TIMEOUT 30  // 30초 idle timeout

void
handle_client(int clnt_sock, int session_id, struct sockaddr_in clnt_addr, int parent_pipe)
{
    signal(SIGPIPE, SIG_IGN);
    
    // Non-blocking 모드 설정
    int flags = fcntl(clnt_sock, F_GETFL, 0);
    fcntl(clnt_sock, F_SETFL, flags | O_NONBLOCK);
    
    char buf[BUF_SIZE];
    char msg_to_parent[BUF_SIZE];
    int str_len;
    int count = 0;
    int write_result;
    time_t last_active = time(NULL);  
    
    printf("  [자식 #%d (PID:%d)] 시작\n", session_id, getpid());

    // 부모에게 시작 알림
    snprintf(msg_to_parent, BUF_SIZE, "CHILD_START:Session#%d,PID:%d", session_id, getpid());
    write_result = write(parent_pipe, msg_to_parent, strlen(msg_to_parent));
    if (write_result == -1)
    {
        fprintf(stderr, "  [자식 #%d] 부모에게 메시지 전송 실패: %s\n", 
                session_id, strerror(errno));
    }
    else if (write_result != (int)strlen(msg_to_parent))
    {
        fprintf(stderr, "  [자식 #%d] 부모에게 부분 write: %d/%lu bytes\n", 
                session_id, write_result, strlen(msg_to_parent));
    }

    // 클라이언트가 연결을 끊을 때까지 계속 처리
    while (1)
    {
        str_len = read(clnt_sock, buf, BUF_SIZE - 1);
        
        if (str_len > 0)
        {
            last_active = time(NULL);  // 활동 시간 갱신
            buf[str_len] = 0;
            // Echo back 
            int sent = 0;
            while (sent < str_len)
            {
                write_result = write(clnt_sock, buf + sent, str_len - sent);
                
                if (write_result == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;
                    }
                    else
                    {
                        fprintf(stderr, "  [자식 #%d] write() error: %s\n", 
                                session_id, strerror(errno));
                        goto cleanup;
                    }
                }
                else
                {
                    sent += write_result;
                }
            }
            
            count++;
        }
        else if (str_len == 0)
        {
            printf("  [자식 #%d] 클라이언트 정상 연결 종료 (EOF)\n", session_id);
            break;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Idle timeout 체크
                if (time(NULL) - last_active > IDLE_TIMEOUT)
                {
                    printf("  [자식 #%d] Idle timeout (%d초), 연결 종료\n", 
                           session_id, IDLE_TIMEOUT);
                    break;
                }
                continue;
            }
            else
            {
                fprintf(stderr, "  [자식 #%d] read() error: %s\n", 
                        session_id, strerror(errno));
                break;
            }
        }
    }

cleanup:
    printf("  [자식 #%d (PID:%d)] %d I/O 완료, 종료\n", 
           session_id, getpid(), count);
    
    // 부모에게 종료 알림
    snprintf(msg_to_parent, BUF_SIZE, "CHILD_END:Session#%d,IO_Count:%d", session_id, count);
    write_result = write(parent_pipe, msg_to_parent, strlen(msg_to_parent));
    if (write_result == -1)
    {
        fprintf(stderr, "  [자식 #%d] 부모에게 종료 메시지 전송 실패: %s\n", 
                session_id, strerror(errno));
    }
    
    if (close(clnt_sock) == -1)
    {
        fprintf(stderr, "  [자식 #%d] close(clnt_sock) error: %s\n", 
                session_id, strerror(errno));
    }
    
    if (close(parent_pipe) == -1)
    {
        fprintf(stderr, "  [자식 #%d] close(parent_pipe) error: %s\n", 
                session_id, strerror(errno));
    }
}