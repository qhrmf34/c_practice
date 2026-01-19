#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

// 서버 소켓 생성 (blocking 모드)
int
create_server_socket(void)
{
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;

    // 소켓 생성 (기본적으로 blocking)
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
    {
        fprintf(stderr, "[에러] socket() 생성 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        return -1;
    }
    
    // SO_REUSEADDR 설정
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
    {
        fprintf(stderr, "[에러] setsockopt(SO_REUSEADDR) 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        close(serv_sock);
        return -1;
    }

    // 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // bind
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        if (errno == EADDRINUSE)
        {
            fprintf(stderr, "[에러] bind() 실패: 포트 %d가 이미 사용 중 (EADDRINUSE)\n", PORT);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "[에러] bind() 실패: 권한 없음 (EACCES)\n");
        }
        else
        {
            fprintf(stderr, "[에러] bind() 실패: %s (errno: %d)\n", 
                    strerror(errno), errno);
        }
        close(serv_sock);
        return -1;
    }

    // listen
    if (listen(serv_sock, 128) == -1)
    {
        fprintf(stderr, "[에러] listen() 실패: %s (errno: %d)\n", 
                strerror(errno), errno);
        close(serv_sock);
        return -1;
    }

    printf("[서버] %d 포트 바인딩 및 리스닝 완료 (blocking 모드)\n", PORT);    
    return serv_sock;
}