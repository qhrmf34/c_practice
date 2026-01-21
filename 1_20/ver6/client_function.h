#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H

#include <arpa/inet.h>

#define BUF_SIZE 1024
#define IO_COUNT 10
#define POLL_TIMEOUT 10000

typedef struct {
    volatile sig_atomic_t running;
    int client_id;
} ClientContext;

void 
client_run(const char *ip, int port, ClientContext *ctx);
int 
client_connect(int argc, char *argv[]);

#endif
