#ifndef CLIENT_FUNCTION_H
#define CLIENT_FUNCTION_H
#ifdef __cplusplus
extern "C" {
#endif
#include <arpa/inet.h>
#include <signal.h>

#define BUF_SIZE 1024
#define IO_COUNT 10
#define POLL_TIMEOUT 10000

typedef struct {
    volatile sig_atomic_t running;                                               /* 클라이언트 실행 상태 플래그 */
} ClientState;

extern void             client_run(const char *ip, int port, int client_id, ClientState *state);
extern int              client_connect(int argc, char *argv[]);
#ifdef __cplusplus

}
#endif

#endif
