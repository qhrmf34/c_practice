#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <poll.h>
#include <errno.h>
#include <string.h>

// poll 에러 처리 - 0: 정상, -1: 치명적 에러
int 
handle_poll_error(int revents, const char *context)
{
    if (!(revents & (POLLERR | POLLHUP | POLLNVAL)))
        return 0; // 에러 없음
    
    if (revents & POLLERR)
        log_message(LOG_ERROR, "%s: POLLERR 발생", context);
    if (revents & POLLHUP)
        log_message(LOG_ERROR, "%s: POLLHUP 발생", context);
    if (revents & POLLNVAL)
        log_message(LOG_ERROR, "%s: POLLNVAL 발생", context);
    
    return -1; // 에러 있음
}

// I/O 에러 처리 - 0: 재시도, -1: 치명적
int 
handle_io_error(int error_num, const char *operation)
{
    if (error_num == EINTR || error_num == EAGAIN || error_num == EWOULDBLOCK) 
    {
        log_message(LOG_DEBUG, "%s: 재시도 가능 에러 (%s)", operation, strerror(error_num));
        return 0; // 재시도
    }
    if (error_num == EPIPE) 
    {
        log_message(LOG_WARNING, "%s: 연결 끊김 (EPIPE)", operation);
        return -1;
    }
    log_message(LOG_ERROR, "%s: %s", operation, strerror(error_num));
    return -1;
}
