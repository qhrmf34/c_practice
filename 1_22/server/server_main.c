#include "server_function.h"
#include <poll.h>
#include <sys/socket.h>

void 
run_server(void)
{
    int serv_sock, clnt_sock, session_id = 0;   // 소켓 및 세션 ID 변수 선언
    struct sockaddr_in serv_addr, clnt_addr;    // 서버/클라이언트 주소 구조체
    int option = 1;                             // 소켓 옵션 설정을 위한 값
    ServerState state = {0};                    //서버상태 초기화
    state.running = 1;                          //서버 실행 루프 플래그 활성화
    state.start_time = time(NULL);              // 서버 시작 시각 기록
    state.parent_pid = getpid();                //crash_handler에서 부모 확인용
    state.log_fd = -1;                          // 로그 파일 디스크립터 초기값 설정
    setup_signal_handlers(&state);              // 시그널 핸들러 및 g_state 연결
    log_init(&state);                           // 로그 시스템 시작 및 파일 열기
    log_message(&state, LOG_INFO, "=== Multi-Process Echo Server 시작 ===");
    log_message(&state, LOG_INFO, "Port: %d", PORT);
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);// TCP 소켓 생성
    if (serv_sock == -1) 
    {
        log_message(&state, LOG_ERROR, "run_server() : socket() 생성 실패: %s", strerror(errno));
        log_close(&state);
        return;
    }
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) // 종료 후 즉시 재시작 가능하게 설정
    {
        log_message(&state, LOG_ERROR, "run_server() : setsockopt(SO_REUSEADDR) 실패: %s", strerror(errno));
        close(serv_sock);
        log_close(&state);
        return;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;                 // IPv4 주소 체계 설정
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 모든 인터페이스의 IP 허용
    serv_addr.sin_port = htons(PORT);               // 지정된 포트 번호 설정
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)         // 소켓에 IP와 포트 번호 할당
    {
        if (errno == EADDRINUSE || errno == EACCES)
            log_message(&state, LOG_ERROR, "run_server() : bind() 실패: 포트가 이미 사용 중");
        else
            log_message(&state, LOG_ERROR, "run_server() : bind() 실패: %s", strerror(errno));
        close(serv_sock);
        log_close(&state);
        return;
    }
    if (listen(serv_sock, 128) == -1)               // 연결 대기 큐 생성 및 대기 상태 진입
    {
        log_message(&state, LOG_ERROR, "run_server() : listen() 실패: %s", strerror(errno));
        close(serv_sock);
        log_close(&state);
        return;
    }
    log_message(&state, LOG_INFO, "클라이언트 연결 대기 중");
    struct pollfd pfd = {.fd = serv_sock, .events = POLLIN, .revents = 0};
    while (state.running)                           //running값 확인(직접참조)
    {
        handle_child_died(&state);                  // 자식 프로세스(좀비) 종료 여부 확인
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 1000);              // 1초 동안 이벤트 대기
        if (ret == -1) 
        {
            if (errno == EINTR)                     //시그널 발생시 continue, state.running값 확인 후 진행
                continue;
            log_message(&state, LOG_ERROR, "run_server() : poll() 실패: %s", strerror(errno));
            continue;
        } 
        else if (ret == 0) 
            continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
        {
            log_message(&state, LOG_ERROR, "run_server() : 서버 소켓 에러: 0x%x", pfd.revents);
            continue;
        } 
        else if (pfd.revents & POLLIN) 
        {
            socklen_t addr_size = sizeof(clnt_addr);   
            clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);                // 클라이언트와 실제 통신할 소켓 생성
            if (clnt_sock == -1) 
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                 {
                    log_message(&state, LOG_DEBUG, "run_server() : accept() 재시도");
                    continue;
                }
                log_message(&state, LOG_ERROR, "run_server() : accept() 실패: %s", strerror(errno));
                continue;
            }
            session_id++;
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clnt_addr.sin_addr, client_ip, sizeof(client_ip));                  //client ip를 문자열로 바꿔 로그 출력
            log_message(&state, LOG_INFO, "새 연결 수락: %s:%d (Session #%d)", client_ip, ntohs(clnt_addr.sin_port), session_id);
            if (fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr, &state) == -1)   //accept된 소켓을 fork,exec
            {
                close(clnt_sock);
                log_message(&state, LOG_ERROR, "run_server() : Worker 생성 실패 (Session #%d)", session_id);
            }
        } 
        else 
        {
            log_message(&state, LOG_WARNING, "run_server() : 처리 안된 이벤트: 0x%x", pfd.revents);
            continue;
        }
    }
    shutdown_workers(&state);           // 종료 시 실행 중인 워커 정리(자식프로세스)
    if (close(serv_sock) == -1)         // 리스닝 소켓 닫기
        log_message(&state, LOG_ERROR, "run_server() : close(serv_sock) 실패: %s", strerror(errno));
    else
        log_message(&state, LOG_INFO, "서버 소켓 닫기 완료");
    final_cleanup(&state);              // 동적 할당 등 자원 최종 정리
    log_close(&state);
}
