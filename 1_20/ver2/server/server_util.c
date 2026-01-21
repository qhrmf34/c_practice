#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <poll.h>
#include <errno.h>

int poll_socket(int sock, short events, int timeout) 
{
    struct pollfd pfd = 
    {
        .fd = sock,
        .events = events,
        .revents = 0
    };
    
    int ret = poll(&pfd, 1, timeout);
    
    if (ret == -1) 
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return -2;  // 재시도 가능
        return -1;  // 심각한 에러
    }
    
    if (ret == 0) 
        return 0;  // 타임아웃
    
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
    {  // 에러 이벤트
        if (pfd.revents & POLLERR) return -3;
        if (pfd.revents & POLLHUP) return -4;
        if (pfd.revents & POLLNVAL) return -5;
    }
    
    if (pfd.revents & events) 
        return 1;  // 정상 이벤트
    
    return 0;  // 기타
}
