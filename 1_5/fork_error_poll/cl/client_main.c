#include "client.h"

int
main(int argc, char* argv[])
{
    char* server_ip;
    int client_id;
    int sock;
    
    if (argc < 2) 
    {
        printf("%s <SERVER_IP> [CLIENT_ID]\n", argv[0]);
        exit(1);
    }
    
    server_ip = argv[1];
    client_id = (argc >= 3) ? atoi(argv[2]) : (getpid() % 1000);
    
    printf("\n");
    printf("  클라이언트 #%d\n", client_id);
    printf("  서버: %s:%d\n", server_ip, SERVER_PORT);
    printf("  %d번 I/O 테스트\n", IO_COUNT);
    
    /* 서버 연결 */
    sock = connect_to_server(server_ip, SERVER_PORT);
    if (sock < 0) 
    {
        fprintf(stderr, "서버 연결 실패\n");
        exit(1);
    }
    
    /* I/O 세션 실행 */
    run_client_session(sock, client_id);
    
    close(sock);
    return 0;
}