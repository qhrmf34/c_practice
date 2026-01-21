#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/file.h>  // flock 사용

// 로그 레벨을 문자열로 변환
static const char* log_level_string(LogLevel level)
{
    switch(level)
    {
        case LOG_INFO:  return "INFO ";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        case LOG_WARNING: return "WARNING";
        default:        return "UNKN ";
    }
}

// 로그 메시지 출력 (파일 + 콘솔)
void log_message(LogLevel level, const char* format, ...)
{
    va_list args;
    time_t now;
    char timestr[64];
    char buffer[2048];  // 전체 로그 메시지 버퍼
    
    // 현재 시간 생성
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 로그 메시지 구성
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // 1. 콘솔 출력 (기존처럼)
    printf("[%s] [%s] [PID:%d] %s\n", 
           timestr, log_level_string(level), getpid(), buffer);
    
    // 2. 파일 출력
    FILE* fp = fopen(LOG_FILE, "a");  // append 모드
    if (fp)
    {
        //  파일 잠금 (여러 프로세스가 동시에 쓸 때 안전)
        flock(fileno(fp), LOCK_EX);
        
        fprintf(fp, "[%s] [%s] [PID:%d] %s\n", 
                timestr, log_level_string(level), getpid(), buffer);
        
        // 즉시 디스크에 쓰기 (tail -f가 즉시 볼 수 있도록)
        fflush(fp);
        
        // 파일 잠금 해제
        flock(fileno(fp), LOCK_UN);
        
        fclose(fp);
    }
}

// 로그 파일 초기화 (서버 시작 시 호출)
void log_init(void)
{
    FILE* fp = fopen(LOG_FILE, "w");  // 기존 로그 삭제
    if (fp)
    {
        fprintf(fp, "=== Server Log Started ===\n");
        fclose(fp);
    }
    
    log_message(LOG_INFO, "로그 시스템 초기화 완료");
}