#include "server.h"

/* 자식 프로세스 메인 로직 */
void
run_child_process(int client_sock)
{
    char* buffer;
    int bytes_read, bytes_written;
    int total_messages = 0;
    time_t start_time;
    
    log_info("[자식] 시작 PID=%d, fd=%d", getpid(), client_sock);
    
    signal(SIGPIPE, SIG_IGN);
    
    /* 버퍼 할당 */
    buffer = (char*)tracked_malloc(BUFFER_SIZE, "child_process");
    if (!buffer) 
    {
        log_info("[자식] 버퍼 할당 실패");
        close(client_sock);
        return;
    }
    
    start_time = time(NULL);
    
    /*  BLOCKING I/O 루프  */
    while (1) 
    {
        /* read */
        bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
        
        if (bytes_read > 0) 
        {
            /* 정상 수신 */
            buffer[bytes_read] = '\0';
            total_messages++;
            
            /* write (에코) */
            int total_sent = 0;
            while (total_sent < bytes_read) 
            {
                bytes_written = write(client_sock, 
                                     buffer + total_sent, 
                                     bytes_read - total_sent);
                
                if (bytes_written < 0) 
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) 
                    {
                        continue;  /* BLOCKING 모드 방어 코드 */
                    }
                    else if (errno == EINTR) 
                    {
                        continue;  /* 시그널 인터럽트 */
                    }
                    else if (errno == EPIPE) 
                    {
                        log_info("[자식] write EPIPE");
                        goto cleanup;
                    }
                    else 
                    {
                        log_info("[자식] write 에러: %s", strerror(errno));
                        goto cleanup;
                    }
                }
                
                total_sent += bytes_written;
            }
        }
        else if (bytes_read == 0) 
        {
            /* EOF */
            log_info("[자식] 클라이언트 종료 (EOF)");
            break;
        }
        else 
        {
            /* read 에러 */
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                continue;  /* BLOCKING 모드 방어 코드 */
            }
            else if (errno == EINTR) 
            {
                continue;  /* 시그널 인터럽트 */
            }
            else 
            {
                log_info("[자식] read 에러: %s", strerror(errno));
                break;
            }
        }
    }

cleanup:
    time_t elapsed = time(NULL) - start_time;
    log_info("[자식] 종료: %d개 메시지, %ld초", total_messages, elapsed);
    
    close(client_sock);
    tracked_free(buffer, BUFFER_SIZE, "child_process");
}