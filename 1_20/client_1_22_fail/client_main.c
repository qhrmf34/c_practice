#define _POSIX_C_SOURCE 200809L
#include "client_function.h"

int 
client_main(int argc, char *argv[])
{
    char *ip;
    int port, client_id, iteration = 0;
    if (argc != 3 && argc != 4)                                                  /* 인자 개수 확인 */
    {
        printf("Usage: %s <IP> <port> [client_id]\n", argv[0]);
        exit(1);
    }
    ip = argv[1];
    port = atoi(argv[2]);                                                        /* 포트 번호 파싱 */
    if (port <= 0 || port > 65535)                                               /* 포트 범위 검증 (1-65535) */
    {
        fprintf(stderr, "client_main() : Error: Invalid port number %d\n", port);
        exit(1);
    }
    if (argc == 4)
        client_id = atoi(argv[3]);                                               /* client_id가 주어진 경우 */
    else
        client_id = getpid() % 1000;                                             /* 없으면 PID로 생성 */
    printf("=== 클라이언트 #%d 시작 ===\n", client_id);
    printf("서버: %s:%d\n", ip, port);
    ClientState state = {0};
    state.running = 1;
    state.state = SESSION_IDLE;                                                  /* 초기 상태: IDLE (연결 가능) */
    setup_client_signal_handlers(&state);
    /* 재연결 루프: SIGINT 받거나 SESSION_CLOSED 상태가 될 때까지 반복 */
    while (state.running && state.state != SESSION_CLOSED) 
    {
        state.state = SESSION_ACTIVE;                                            /* 세션 활성화 */
        iteration++;
        printf("\n[클라이언트 #%d] ===== 반복 #%d =====\n", client_id, iteration);
        int sock = -1;
        struct sockaddr_in serv_addr;
        char msg[BUF_SIZE], recv_buf[BUF_SIZE];
        time_t start_time, end_time;
        int count = 0;
        start_time = time(NULL);
        /* 소켓 생성 */
        sock = socket(PF_INET, SOCK_STREAM, 0);                                  /* TCP 소켓 생성 */
        if (sock == -1)                                                          /* socket 실패: fd 한계, 권한, 메모리 */
        {
            fprintf(stderr, "client_main() : [클라이언트 #%d] socket() 실패: %s\n", client_id, strerror(errno));
            state.state = SESSION_CLOSED;                                        /* 소켓 생성 실패는 치명적 */
            break;
        }
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0)                    /* IP 문자열을 네트워크 주소로 변환 */
        {
            fprintf(stderr, "client_main() : [클라이언트 #%d] 잘못된 IP 주소: %s\n", client_id, ip);  /* 변환 실패: 잘못된 IP 형식 */
            close(sock);
            state.state = SESSION_CLOSED;                                        /* IP 형식 오류는 치명적 */
            break;
        }
        serv_addr.sin_port = htons(port);
        /* 서버 연결 */
        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)  /* 서버에 연결 */
        {
            fprintf(stderr, "client_main() : [클라이언트 #%d] connect() 실패: %s\n", client_id, strerror(errno));  /* 연결 실패: 서버 없음, 네트워크 에러, 방화벽 */
            close(sock);
            state.state = SESSION_ACTIVE;                                          /* 연결 실패는 재시도 가능 */
            break;
        }
        printf("[클라이언트 #%d] 서버 연결 성공!\n", client_id);
        struct pollfd read_pfd = {.fd = sock, .events = POLLIN, .revents = 0};
        /* I/O 루프 */
        while (count < IO_COUNT && state.running && state.state == SESSION_ACTIVE) 
        {
            if (!state.running)                                                  /* 종료 신호 확인 */
            {
                printf("[클라이언트 #%d] 중단 (%d/%d 완료)\n", client_id, count, IO_COUNT);
                state.state = SESSION_CLOSING;                                   /* 종료 중 */
                break;
            }
            snprintf(msg, BUF_SIZE, "[Client #%d] Message #%d at %ld\n", client_id, count + 1, time(NULL));
            /* 메시지 전송 */
            ssize_t sent = 0;
            int msg_len = strlen(msg);
            while (sent < msg_len && state.running) 
            {
                ssize_t write_result = write(sock, msg + sent, msg_len - sent);
                if (write_result == -1)                                          /* write 에러 */
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)  /* 재시도 가능 */
                        continue;
                    if (errno == EPIPE)                                          /* 서버가 연결 끊음 */
                    {
                        fprintf(stderr, "client_main() : [클라이언트 #%d] write() EPIPE: 서버 연결 끊김\n", client_id);
                        state.state = SESSION_IDLE;                              /* 재연결 가능 */
                        break;
                    }
                    fprintf(stderr, "client_main() : [클라이언트 #%d] write() 실패: %s\n", client_id, strerror(errno));
                    state.state = SESSION_IDLE;                                  /* 재연결 시도 */
                    break;
                }
                sent += write_result;                                            /* 전송한 바이트 수 누적 */
            }
            /* 에코 응답 수신 */
            read_pfd.revents = 0;
            int read_ret = poll(&read_pfd, 1, POLL_TIMEOUT);                     /* 읽기 대기 (10초 타임아웃) */
            if (read_ret == -1)                                                  /* poll 실패 */
            {
                if (errno == EINTR)                                              /* 시그널로 중단 */
                {
                    if (!state.running) 
                    {
                        printf("[클라이언트 #%d] 수신 대기 중 중단\n", client_id);
                        state.state = SESSION_CLOSING;
                        break;
                    }
                    continue;
                }
                fprintf(stderr, "client_main() : [클라이언트 #%d] poll() 실패: %s\n", client_id, strerror(errno));
                state.state = SESSION_IDLE;                                      /* 재연결 시도 */
                break;
            }
            else if (read_ret == 0)                                              /* 타임아웃 */
            {
                fprintf(stderr, "client_main() : [클라이언트 #%d] poll 타임아웃\n", client_id);
                state.state = SESSION_IDLE;                                      /* 재연결 시도 */
                break;
            }
            if (read_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
            {
                fprintf(stderr, "client_run() : [클라이언트 #%d] poll 에러 이벤트: 0x%x\n", client_id, read_pfd.revents);
                break;
            } 
            else if (read_pfd.revents & POLLIN)                                  /* 읽을 데이터 있음 */
            {
                ssize_t str_len = read(sock, recv_buf, BUF_SIZE - 1);
                if (str_len > 0)                                                 /* 데이터 수신 성공 */
                {
                    recv_buf[str_len] = 0;                                       /* NULL 종료 문자 */
                    printf("[클라이언트 #%d] 수신: %s", client_id, recv_buf);
                } 
                else if (str_len == 0)                                           /* EOF: 서버가 연결 종료 */
                {
                    fprintf(stderr, "client_main() : [클라이언트 #%d] 서버 연결 종료 (EOF)\n", client_id);
                    state.state = SESSION_IDLE;                                  /* 재연결 시도 */
                    break;
                } 
                else                                                             /* read 에러 */
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)  /* 재시도 가능 */
                        continue;
                    fprintf(stderr, "client_main() : [클라이언트 #%d] read() 실패: %s\n", client_id, strerror(errno));
                    state.state = SESSION_IDLE;                                  /* 재연결 시도 */
                    break;
                }
            }
            count++;                                                             /* I/O 완료 카운트 증가 */
        }
        /* 세션 종료 처리 */
        end_time = time(NULL);
        if (count >= IO_COUNT && state.state == SESSION_ACTIVE)                  /* 정상 완료 */
        {
            printf("[클라이언트 #%d] 완료: %d I/O, %ld초\n", client_id, count, end_time - start_time);
            state.state = SESSION_IDLE;                                          /* 재연결 가능 상태로 */
        }
        else if (state.state == SESSION_CLOSING)                                 /* SIGINT로 중단 */
        {
            printf("[클라이언트 #%d] 중단: %d/%d I/O, %ld초\n", client_id, count, IO_COUNT, end_time - start_time);
            state.state = SESSION_CLOSED;                                        /* 완전 종료 */
        }
        else                                                                     /* 에러로 인한 종료 (state는 이미 IDLE로 설정됨) */
        {
            printf("[클라이언트 #%d] 에러: %d/%d I/O, %ld초\n", client_id, count, IO_COUNT, end_time - start_time);
            /* state.state는 이미 SESSION_IDLE (재연결 시도) */
        }
        close(sock);                                                             /* 소켓 닫기 */                                                 /* 1초 대기 */
    }
    printf("\n[클라이언트 #%d] 최종 종료\n", client_id);
    return 0;
}