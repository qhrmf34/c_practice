#include "client.h"

/* 서버 연결 */
int
connect_to_server(const char* server_ip, int port)
{
    int sock;
    struct sockaddr_in server_addr;
    
    /* 소켓 생성 */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket()");
        return -1;
    }
    
    /* 서버 주소 설정 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "잘못된 IP 주소: %s\n", server_ip);
        close(sock);
        return -1;
    }
    
    /* 서버 연결 (BLOCKING) */
    printf("[연결] 서버 연결 중...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        close(sock);
        return -1;
    }
    
    printf("[연결] 성공! I/O 시작\n\n");
    return sock;
}

/* 클라이언트 I/O 세션 */
int
run_client_session(int sock, int client_id)
{
    char send_buf[BUFFER_SIZE];
    char recv_buf[BUFFER_SIZE];
    int i;
    time_t start_time, end_time;
    
    start_time = time(NULL);
    
    /* I/O 루프 (BLOCKING) */
    for (i = 0; i < IO_COUNT; i++) {
        /* 메시지 생성 */
        snprintf(send_buf, BUFFER_SIZE, 
                 "[Client #%d] Message #%d at %ld", 
                 client_id, i + 1, time(NULL));
        
        /* 송신 - 완전 전송 보장 */
        int msg_len = strlen(send_buf);
        int total_sent = 0;
        
        while (total_sent < msg_len) {
            int n = write(sock, send_buf + total_sent, msg_len - total_sent);
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;  
                }
                else if (errno == EINTR) {
                    continue;  
                }
                else if (errno == EPIPE) {
                    printf("[클라이언트 #%d] write EPIPE (연결 끊김)\n", client_id);
                    return -1;
                }
                else {
                    perror("write()");
                    return -1;
                }
            }
            
            total_sent += n;
        }
        
        /* 수신 */
        while (1) {
            int n = read(sock, recv_buf, BUFFER_SIZE - 1);
            
            if (n > 0) {
                /* 정상 수신 */
                recv_buf[n] = '\0';
                printf("[클라이언트 #%d] Echo: %s\n", client_id, recv_buf);
                break;
            }
            else if (n == 0) {
                /* EOF */
                printf("[클라이언트 #%d] 서버 종료 (EOF)\n", client_id);
                return -1;
            }
            else {
                /* read 에러 */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;  
                }
                else if (errno == EINTR) {
                    continue;  
                }
                else {
                    perror("read()");
                    return -1;
                }
            }
        }
    }
    
    end_time = time(NULL);
    
    printf("\n[클라이언트 #%d] 완료: %d회 I/O, %ld초\n", 
           client_id, i, end_time - start_time);
    
    return 0;
}