#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

int
create_server_socket(void)
{
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);                                 /* TCP 소켓 생성 */
    if (serv_sock == -1)                                                         /* socket 실패: fd 한계, 권한 없음, 메모리 부족 */
    {
        fprintf(stderr, "[에러] socket() 생성 실패: %s\n", strerror(errno));
        return -1;
    }
    
    /* SO_REUSEADDR: TIME_WAIT 상태 소켓 재사용 허용 (서버 재시작 시 필수) */
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) 
    {
        fprintf(stderr, "[에러] setsockopt(SO_REUSEADDR) 실패: %s\n", strerror(errno));  /* 실패: 잘못된 소켓, 메모리 */
        close(serv_sock);
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);                               /* 모든 인터페이스에서 수신 */
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)  /* 소켓을 포트에 바인딩 */
    {
        if (errno == EADDRINUSE)                                                 /* 포트가 이미 사용 중 (가장 흔한 경우) */
            fprintf(stderr, "[에러] bind() 실패: 포트 %d가 이미 사용 중\n", PORT);
        else if (errno == EACCES)                                                /* 권한 없음 (1024 이하 포트는 root 필요) */
            fprintf(stderr, "[에러] bind() 실패: 권한 없음\n");
        else
            fprintf(stderr, "[에러] bind() 실패: %s\n", strerror(errno));        /* 기타: 잘못된 주소, 소켓 오류 */
        close(serv_sock);
        return -1;
    }

    if (listen(serv_sock, 128) == -1)                                            /* 연결 대기 큐 생성 (backlog=128) */
    {
        fprintf(stderr, "[에러] listen() 실패: %s\n", strerror(errno));          /* 실패: 소켓 오류, 이미 listening */
        close(serv_sock);
        return -1;
    }

    printf("[서버] %d 포트 바인딩 및 리스닝 완료\n", PORT);
    return serv_sock;
}
