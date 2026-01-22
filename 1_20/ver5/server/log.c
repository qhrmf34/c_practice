#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static const char*
log_level_string(LogLevel level)
{
    switch(level) 
    {
        case LOG_INFO: return "INFO ";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        case LOG_WARNING: return "WARNING";
        default: return "UNKN ";
    }
}

void
log_message(LogContext *ctx, LogLevel level, const char* format, ...)
{
    va_list args;
    time_t now;
    char timestr[64];
    char buffer[2048];
    char log_line[2200];
    
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    int len = snprintf(log_line, sizeof(log_line), "[%s] [%s] [PID:%d] %s\n", 
                       timestr, log_level_string(level), getpid(), buffer);
    
    /* snprintf 반환값 클램프 (OOB read 방지) */
    if (len < 0)                                                                 /* snprintf 에러 (거의 없지만) */
        len = 0;
    else if (len >= (int)sizeof(log_line))                                       /* 버퍼보다 길면 잘림 */
        len = sizeof(log_line) - 1;                                              /* 실제 버퍼 크기로 제한 */
    
    write(STDOUT_FILENO, log_line, len);
    
    if (ctx && ctx->fd >= 0)
        write(ctx->fd, log_line, len);
}


void
log_init(LogContext *ctx)
{
    if (ctx == NULL)
        return;
        
    ctx->fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644); //로그파일이 없으면 생성. 항상 파일끝에 붙여씀.
    if (ctx->fd == -1) 
    {
        fprintf(stderr, "로그 파일 열기 실패: %s\n", LOG_FILE, strerror(errno));
        return;
    }
    
    /* FD_CLOEXEC 설정: exec 시 자동으로 닫힘 (FD 누수 방지) */
    int flags = fcntl(ctx->fd, F_GETFD);                                         /* 현재 FD 플래그 조회 */
    if (flags != -1)                                                             /* fcntl 성공 시 */
    {
        if (fcntl(ctx->fd, F_SETFD, flags | FD_CLOEXEC) == -1)                   /* CLOEXEC 추가 */
            fprintf(stderr, "FD_CLOEXEC 설정 실패: %s\n", strerror(errno));      /* 실패해도 치명적이진 않음 */
    }
    
    const char *header = "=== Server Log Started ===\n";
    write(ctx->fd, header, strlen(header));
    log_message(ctx, LOG_INFO, "로그 시스템 초기화 완료");
}

void
log_close(LogContext *ctx)
{
    if (ctx == NULL || ctx->fd < 0)                                              /* 이미 닫혔거나 없는 경우 */
        return;
        
    const char *footer = "=== Server Log Closed ===\n";
    write(ctx->fd, footer, strlen(footer));                                      /* 로그 종료 헤더 기록 */
    
    if (close(ctx->fd) == -1)                                                    /* close() 실패는 드물지만 가능 (EIO 등) */
        fprintf(stderr, "로그 파일 닫기 실패\n");
    
    ctx->fd = -1;                                                                /* 재사용 방지 */
}
