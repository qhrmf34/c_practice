#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * 로그 레벨을 문자열로 변환
 */
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

/*
 * 로그 메시지 출력
 * 
 * @param ctx: 로그 컨텍스트 (fd 포함)
 * @param level: 로그 레벨
 * @param format: printf 형식 문자열
 * @param ...: 가변 인자
 * 
 * 동작:
 * 1. 현재 시간 + PID + 레벨 + 메시지 조합
 * 2. 콘솔 출력 (STDOUT)
 * 3. 파일 출력 (fd가 유효한 경우)
 * 
 * 특징:
 * - write() 사용으로 성능 향상 (fopen/fclose 없음)
 * - Multi-process safe (O_APPEND 모드)
 */
void
log_message(LogContext *ctx, LogLevel level, const char* format, ...)
{
    va_list args;
    time_t now;
    char timestr[64];
    char buffer[2048];
    char log_line[2200];
    
    // 현재 시간 문자열 생성
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 가변 인자 처리
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // 로그 라인 조합: [시간] [레벨] [PID] 메시지
    int len = snprintf(log_line, sizeof(log_line), "[%s] [%s] [PID:%d] %s\n", timestr, log_level_string(level), getpid(), buffer);
    
    // 콘솔 출력 (항상)
    write(STDOUT_FILENO, log_line, len);
    
    // 파일 출력 (fd가 유효한 경우만)
    if (ctx && ctx->fd >= 0)
        write(ctx->fd, log_line, len);
}

/*
 * 로그 시스템 초기화
 * 
 * @param ctx: 로그 컨텍스트 (초기화할 구조체)
 * 
 * 동작:
 * 1. 로그 파일 열기 (O_APPEND 모드)
 * 2. 헤더 메시지 출력
 * 3. 초기화 완료 로그
 * 
 * 특징:
 * - O_APPEND: Multi-process에서 안전하게 추가
 * - fd 유지: 매번 open/close하지 않음 (성능)
 */
void
log_init(LogContext *ctx)
{
    if (ctx == NULL) 
    {
        fprintf(stderr, "log_init: ctx가 NULL\n");
        return;
    }
    
    // 로그 파일 열기 (쓰기+생성+추가 모드)
    ctx->fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (ctx->fd == -1) 
    {
        fprintf(stderr, "로그 파일 열기 실패: %s\n", LOG_FILE);
        return;
    }
    
    // 콘솔 출력 활성화
    ctx->console_enabled = 1;
    
    // 헤더 메시지
    const char *header = "=== Server Log Started ===\n";
    write(ctx->fd, header, strlen(header));
    
    // 초기화 완료 로그
    log_message(ctx, LOG_INFO, "로그 시스템 초기화 완료 (fd=%d)", ctx->fd);
}

/*
 * 로그 시스템 정리
 * 
 * @param ctx: 로그 컨텍스트
 * 
 * 동작:
 * - 로그 파일 FD 닫기
 * - 구조체 초기화
 */
void
log_cleanup(LogContext *ctx)
{
    if (ctx == NULL)
        return;
    
    if (ctx->fd >= 0) 
    {
        log_message(ctx, LOG_INFO, "로그 시스템 종료");
        close(ctx->fd);
        ctx->fd = -1;
    }
}
