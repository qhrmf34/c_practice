#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>

#define SERVER_PORT 9190
#define BUFFER_SIZE 4096
#define IO_COUNT 10

/* client_io.c */
int 
connect_to_server(const char* server_ip, int port);

int 
run_client_session(int sock, int client_id);

#endif