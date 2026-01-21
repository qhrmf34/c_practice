#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/file.h>

static const char*
log_level_string(LogLevel level)
{
    switch(level) {
        case LOG_INFO: return "INFO ";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        case LOG_WARNING: return "WARNING";
        default: return "UNKN ";
    }
}

void
log_message(LogLevel level, const char* format, ...)
{
    va_list args;
    time_t now;
    char timestr[64];
    char buffer[2048];
    
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    printf("[%s] [%s] [PID:%d] %s\n", timestr, log_level_string(level), getpid(), buffer);
    
    FILE* fp = fopen(LOG_FILE, "a");
    if (fp) {
        flock(fileno(fp), LOCK_EX);
        fprintf(fp, "[%s] [%s] [PID:%d] %s\n", timestr, log_level_string(level), getpid(), buffer);
        fflush(fp);
        flock(fileno(fp), LOCK_UN);
        fclose(fp);
    }
}

void
log_init(void)
{
    FILE* fp = fopen(LOG_FILE, "w");
    if (fp) {
        fprintf(fp, "=== Server Log Started ===\n");
        fclose(fp);
    }
    log_message(LOG_INFO, "로그 시스템 초기화 완료");
}
