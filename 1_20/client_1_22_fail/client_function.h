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
typedef enum 
{
    SESSION_IDLE = 0,                                                            /* 연결 가능 상태 (재연결 대기) */
    SESSION_ACTIVE,                                                              /* 활성 연결 중 */
    SESSION_CLOSING,                                                             /* 종료 중 (SIGINT 등) */
    SESSION_CLOSED                                                               /* 완전 종료 (프로그램 종료) */
} SessionState;
typedef struct 
{
    SessionState state;                                                          /* 세션 상태 */
    volatile sig_atomic_t running;                                               /* 실행 상태 플래그 */
} ClientState;
extern int              client_main(int argc, char *argv[]);                     /* 통합된 메인 함수 */
extern void             setup_client_signal_handlers(ClientState *state);
#ifdef __cplusplus
}
#endif
#endif