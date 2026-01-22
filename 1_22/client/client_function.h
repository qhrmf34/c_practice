#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BUF_SIZE 1024
#define IO_COUNT 10
#define POLL_TIMEOUT 10000
typedef struct 
{
    volatile sig_atomic_t running;
} ClientState;
extern void         client_run(const char *ip, int port, int client_id, ClientState *state);
extern int          client_connect(int argc, char *argv[]);
extern void         setup_client_signal_handlers(ClientState *state);
#ifdef __cplusplus
}
#endif
#endif
