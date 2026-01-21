#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H

#include <arpa/inet.h>

#define BUF_SIZE 1024
#define IO_COUNT 10
#define POLL_TIMEOUT 10000

void client_run(const char *ip, int port, int client_id);
int client_connect(int argc, char *argv[]);

#endif
