#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

int create_server_socket(void) {
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) return -1;
    
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        close(serv_sock);
        return -1;
    }

    if (listen(serv_sock, 128) == -1) {
        close(serv_sock);
        return -1;
    }

    printf("[서버] %d 포트 바인딩 완료\n", PORT);
    return serv_sock;
}
