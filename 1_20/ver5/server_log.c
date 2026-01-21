#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

static int log_fd = -1;

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
    char log_line[2200];
    
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    int len = snprintf(log_line, sizeof(log_line), "[%s] [%s] [PID:%d] %s\n", 
                       timestr, log_level_string(level), getpid(), buffer);
    
    write(STDOUT_FILENO, log_line, len);
    
    if (log_fd >= 0)
        write(log_fd, log_line, len);
}

void
log_init(void)
{
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        fprintf(stderr, "로그 파일 열기 실패\n");
        return;
    }
    
    const char *header = "=== Server Log Started ===\n";
    write(log_fd, header, strlen(header));
    log_message(LOG_INFO, "로그 시스템 초기화");
}
