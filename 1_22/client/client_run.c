#include "client_function.h"
void 
client_run(const char *ip, int port, int client_id, ClientState *state)
{
    int sock, count = 0;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE], recv_buf[BUF_SIZE];
    time_t start_time, end_time;
    start_time = time(NULL);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) 
    {
        fprintf(stderr, "client_run() : [클라이언트 #%d] socket() 실패: %s\n", client_id, strerror(errno));
        return;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) 
    {
        fprintf(stderr, "client_run() : [클라이언트 #%d] 잘못된 IP 주소: %s\n", client_id, ip);
        close(sock);
        return;
    }
    serv_addr.sin_port = htons(port);
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) 
    {
        fprintf(stderr, "client_run() : [클라이언트 #%d] connect() 실패: %s\n", client_id, strerror(errno));
        close(sock);
        return;
    }
    printf("[클라이언트 #%d] 서버 연결 성공!\n", client_id);
    struct pollfd read_pfd = {.fd = sock, .events = POLLIN, .revents = 0};
    while (count < IO_COUNT && state->running) 
    {
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 중단 (%d/%d 완료)\n", client_id, count, IO_COUNT);
            break;
        }
        snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
        ssize_t sent = 0;
        int msg_len = strlen(msg);
        while (sent < msg_len && state->running) 
        {
            ssize_t write_result = write(sock, msg + sent, msg_len - sent);
            if (write_result == -1) 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                if (errno == EPIPE) 
                {
                    fprintf(stderr, "client_run() : [클라이언트 #%d] write() EPIPE: 서버 연결 끊김\n", client_id);
                    break;
                }
                fprintf(stderr, "client_run() : [클라이언트 #%d] write() 실패: %s\n", client_id, strerror(errno));
                break;
            }
            sent += write_result;
        }
        if (!state->running) 
        {
            printf("[클라이언트 #%d] 전송 완료 후 중단\n", client_id);
            break;
        }
        read_pfd.revents = 0;
        int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);
        if (read_ret == -1) 
        {
            if (errno == EINTR) 
            {
                if (!state->running) 
                {
                    printf("[클라이언트 #%d] 수신 대기 중 중단\n", client_id);
                    break;
                }
                continue;
            }
            fprintf(stderr, "client_run() : [클라이언트 #%d] poll() 실패: %s\n", client_id, strerror(errno));
            break;
        } 
        else if (read_ret == 0) 
        {
            fprintf(stderr, "client_run() : [클라이언트 #%d] poll 타임아웃\n", client_id);
            break;
        }
        if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            fprintf(stderr, "client_run() : [클라이언트 #%d] poll 에러 이벤트: 0x%x\n", client_id, read_pfd.revents);
            break;
        } 
        else if (read_pfd.revents & POLLIN) 
        {
            ssize_t str_len = read(sock, recv_buf, BUF_SIZE - 1);
            if (str_len > 0) 
            {
                recv_buf[str_len] = 0;
                printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
            } 
            else if (str_len == 0) 
            {
                fprintf(stderr, "client_run() : [클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
                break;
            } 
            else 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                fprintf(stderr, "client_run() : [클라이언트 #%d] read() 실패: %s\n", client_id, strerror(errno));
                break;
            }
        }
        count++;
    }
    end_time = time(NULL);
    if (state->running)
        printf("[클라이언트 #%d] 완료: %d I/O, %ld초\n", client_id, count, end_time - start_time);
    else
        printf("[클라이언트 #%d] 중단: %d/%d I/O, %ld초\n", client_id, count, IO_COUNT, end_time - start_time);
    close(sock);
}

int 
client_connect(int argc, char *argv[])
{
    char *ip;
    int port, client_id, iteration = 0;
    if (argc != 3 && argc != 4) 
    {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        exit(1);
    }
    ip = argv[1];
    port = atoi(argv[2]);
    if (port <= 0 || port > 65535) 
    {
        fprintf(stderr, "client_connect() : Error: Invalid port number %d\n", port);
        exit(1);
    }
    if (argc == 4)
        client_id = atoi(argv[3]);
    else
        client_id = getpid() % 1000;
    printf("=== 클라이언트 #%d 시작 ===\n", client_id);
    printf("서버: %s:%d\n", ip, port);
    printf("Ctrl+C로 종료하세요.\n\n");
    ClientState state = {0};
    state.running = 1;
    setup_client_signal_handlers(&state);
    while (state.running) 
    {
        iteration++;
        printf("\n[클라이언트 #%d] ===== 반복 #%d =====\n", client_id, iteration);
        client_run(ip, port, client_id, &state);
    }
    return 0;
}
