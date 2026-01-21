#include "server.h"

/* 전역 변수 정의 */
volatile sig_atomic_t g_server_running = 1;
int g_use_waitpid = 0;

int
main(int argc, char* argv[])
{
    int server_sock;

    printf("   멀티 프로세스 서버\n");
    printf("   Port: %d\n", SERVER_PORT);
    
    /* 명령줄 옵션 파싱 */
    if (argc > 1 && strcmp(argv[1], "--waitpid") == 0) 
    {
        g_use_waitpid = 1;
        log_info("[옵션] waitpid 모드 활성화");
    } 
    else 
    {
        log_info("[옵션] 기본 모드 (좀비 발생)");
        printf("         waitpid 사용: %s --waitpid\n\n", argv[0]);
    }
    
    /* 리소스 모니터 초기화 */
    init_resource_monitor();
    
    /* 시그널 핸들러 설정 */
    setup_signal_handlers();
    
    /* 초기 리소스 상태 */
    print_resource_status("서버 시작");
    
    /* 서버 소켓 생성 */
    server_sock = create_server_socket(SERVER_PORT);
    
    /* 부모 프로세스 실행 */
    run_parent_process(server_sock);
    
    /* 종료 */
    close(server_sock);
    log_info("[서버] 정상 종료");
    
    return EXIT_SUCCESS;
}