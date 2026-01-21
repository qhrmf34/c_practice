#include "server.h"


/* 서버 소켓 생성 */
int
create_server_socket(int port)
{
    int sock;
    struct sockaddr_in addr;
    int opt = 1;
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) 
    {
        error_exit("socket()");
    }
    
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
        error_exit("setsockopt()");
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        if (errno == EADDRINUSE) 
        {
            fprintf(stderr, "[에러] 포트 %d 이미 사용 중\n", port);
        }
        error_exit("bind()");
    }
    
    if (listen(sock, LISTEN_BACKLOG) < 0) 
    {
        error_exit("listen()");
    }
    
    log_info("[부모] 포트 %d 바인딩 완료", port);
    return sock;
}

/* 부모 프로세스 메인 루프 */
void
run_parent_process(int server_sock)
{
    struct pollfd pfd;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    int client_sock;
    pid_t pid;
    
    log_info("[부모] 클라이언트 대기 중 (poll 사용)");
    
    pfd.fd = server_sock;
    pfd.events = POLLIN;
    
    while (g_server_running) 
    {
        /* poll로 신규 연결 대기 */
        int ret = poll(&pfd, 1, 1000);
        
        if (ret < 0) 
        {
            if (errno == EINTR) continue;
            error_exit("poll()");
        }
        
        if (ret == 0) continue;
        if (!(pfd.revents & POLLIN)) continue;
        
        /* accept */
        addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_sock < 0) 
        {
            if (errno == EINTR) continue;
            perror("accept()");
            continue;
        }
        
        log_info("[부모] 새 연결: %s:%d (fd=%d)", 
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port),
                 client_sock);
        
        /* fork */
        pid = fork();
        
        if (pid < 0) 
        {
            perror("fork()");
            close(client_sock);
            print_resource_status("fork 실패");
            continue;
        }
        
        if (pid == 0) 
        {
            /* === 자식 프로세스 === */
            close(server_sock);  /* 서버 소켓 불필요 */
            run_child_process(client_sock);  /* client_sock은 fork로 자동 상속 */
            exit(EXIT_SUCCESS);
        }
        
        /* === 부모 프로세스 === */
        increment_fork_count();
        close(client_sock);  /* 부모는 client_sock 불필요 */
        
        log_info("[부모] 자식 생성: PID=%d (총 %d개)", pid, get_fork_count());
        
        /* 10개마다 리소스 출력 */
        if (get_fork_count() % 10 == 0) 
        {
            print_resource_status("10개 fork 후");
        }
    }
    
    /* 종료 통계 */
    print_server_statistics();
    print_resource_status("서버 종료");
}