#include "server_function.h"
#include <stdarg.h>
#include <fcntl.h>

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
log_message(ServerState *state, LogLevel level, const char* format, ...)
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
    int len = snprintf(log_line, sizeof(log_line), "[%s] [%s] [PID:%d] %s\n", timestr, log_level_string(level), getpid(), buffer);
    if (len < 0)
        len = 0;
    else if (len >= (int)sizeof(log_line))
        len = sizeof(log_line) - 1;
    write(STDOUT_FILENO, log_line, len);
    if (state && state->log_fd >= 0)
        write(state->log_fd, log_line, len);
}
void 
log_init(ServerState *state)
{
    if (state == NULL)
        return;
    state->log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (state->log_fd == -1) 
    {
        fprintf(stderr, "log_init() : 로그 파일 열기 실패: %s\n", strerror(errno));
        return;
    }
    int flags = fcntl(state->log_fd, F_GETFD);
    if (flags != -1) 
    {
        if (fcntl(state->log_fd, F_SETFD, flags | FD_CLOEXEC) == -1)
            fprintf(stderr, "log_init() : FD_CLOEXEC 설정 실패: %s\n", strerror(errno));
    }
    const char *header = "=== Server Log Started ===\n";
    write(state->log_fd, header, strlen(header));
    log_message(state, LOG_INFO, "로그 시스템 초기화 완료");
}
void log_close(ServerState *state)
{
    if (state == NULL || state->log_fd < 0)
        return;
    const char *footer = "=== Server Log Closed ===\n";
    write(state->log_fd, footer, strlen(footer));
    if (close(state->log_fd) == -1)
        fprintf(stderr, "log_close() : 로그 파일 닫기 실패: %s\n", strerror(errno));
    state->log_fd = -1;
}
