#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

int
accept_client(int serv_sock, struct sockaddr_in *clnt_addr, ServerState *state)
{
    socklen_t addr_size = sizeof(*clnt_addr);
    int clnt_sock = accept(serv_sock, (struct sockaddr*)clnt_addr, &addr_size);  /* 새 클라이언트 연결 수락 */
    
    if (clnt_sock == -1)                                                         /* accept 실패 */
    {
        if (!state->running)                                                     /* 종료 신호로 인한 중단 */
        {
            log_message(state->log_ctx, LOG_INFO, "종료 시그널로 인한 accept() 중단");
            return -1;
        }
        
        if (errno == EINTR)                                                      /* 시그널로 인한 중단 (재시도 가능) */
        {
            log_message(state->log_ctx, LOG_DEBUG, "accept() interrupted, 재시도");
            return -1;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)                        /* 일시적으로 연결 불가 (재시도 가능) */
        {
            log_message(state->log_ctx, LOG_DEBUG, "accept() 일시적으로 불가, 재시도");
            return -1;
        }
        
        log_message(state->log_ctx, LOG_ERROR, "accept() 실패: %s", strerror(errno));  /* 기타 에러: 소켓 오류, 메모리 부족, fd 한계 */
        return -1;
    }
    
    return clnt_sock;
}
