#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <sys/socket.h>
#include <errno.h>

int 
accept_client(int serv_sock, struct sockaddr_in *clnt_addr) 
{
    socklen_t addr_size = sizeof(*clnt_addr);
    int clnt_sock = accept(serv_sock, (struct sockaddr*)clnt_addr, &addr_size);
    return clnt_sock;  // -1 on error
}
