#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

int
accept_client(int serv_sock, struct sockaddr_in *clnt_addr)
{
    socklen_t addr_size = sizeof(*clnt_addr);
    int clnt_sock = accept(serv_sock, (struct sockaddr*)clnt_addr, &addr_size);
    
    if (clnt_sock == -1) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        log_message(LOG_ERROR, "accept() 실패: %s", strerror(errno));
        return -1;
    }
    
    return clnt_sock;
}
