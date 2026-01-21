#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>

int 
do_poll_wait(struct pollfd *pfd, int timeout, const char *context)// poll 통합 함수 - 성공:0, 타임아웃:1, 에러:-1
{
    pfd->revents = 0;
    int result = poll(pfd, 1, timeout);
    
    if (result == -1) 
    {
        if (handle_io_error(errno, context) == 0)
            return 1; // 재시도 가능
        return -1; // 치명적 에러
    }
    if (result == 0) 
    {
        log_message(LOG_DEBUG, "%s: 타임아웃", context);
        return 1; // 타임아웃
    }
    if (handle_poll_error(pfd->revents, context) == -1)
        return -1; // 에러 이벤트
    
    return 0; // 성공
}

ssize_t 
safe_read(int fd, void *buf, size_t count, SessionDescriptor *session)// 안전한 read - EOF:0, 에러:-1, 성공:읽은바이트
{
    ssize_t result = read(fd, buf, count);
    
    if (result == 0)
        return 0; // EOF
    if (result < 0) 
    {
        if (handle_io_error(errno, "read") == 0)
            return -2; // 재시도 가능
        return -1; // 치명적 에러
    }
    if (session)
        update_session_activity(session);
    
    return result;
}

ssize_t safe_write_all(int fd, const void *buf, size_t count, SessionDescriptor *session)// 안전한 write (전체 데이터 쓰기) - 성공:0, 에러:-1, 재시도:-2
{
    size_t sent = 0;
    
    while (sent < count) 
    {
        ssize_t result = write(fd, (const char*)buf + sent, count - sent);
        if (result == -1) 
        {
            if (handle_io_error(errno, "write") == 0)
                return -2; // 재시도 가능
            return -1; // 치명적 에러
        }
        sent += result;
    }
    if (session)
        update_session_activity(session);
    
    return 0; // 성공
}

int 
check_session_timeout(SessionDescriptor *session)// 세션 타임아웃 체크 - 타임아웃:1, 정상:0
{
    if (!session)
        return 0;

    time_t current_time = time(NULL);
    if (current_time - session->last_activity >= SESSION_IDLE_TIMEOUT) 
    {
        log_message(LOG_WARNING, "[자식 #%d] idle 타임아웃", session->session_id);
        return 1;
    }
    return 0;
}

void 
update_session_activity(SessionDescriptor *session)// 세션 활동 시간 업데이트
{
    if (session)
        session->last_activity = time(NULL);
}

int 
safe_accept(int serv_sock, struct sockaddr_in *clnt_addr, volatile sig_atomic_t *running)// 안전한 accept - 성공:fd, 재시도:-2, 종료:-1
{
    socklen_t addr_size = sizeof(*clnt_addr);
    int clnt_sock = accept(serv_sock, (struct sockaddr*)clnt_addr, &addr_size);
    
    if (clnt_sock == -1)
    {
        if (!(*running))
            return -1; // 종료중
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return -2; // 재시도
        log_message(LOG_ERROR, "accept() 실패: %s", strerror(errno));
        return -2; // 재시도
    }
    return clnt_sock;
}
