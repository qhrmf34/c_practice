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
    va_list args;                                                       // 가변 인자 처리를 위한 리스트
    time_t now;                                                         // 현재 시간 획득
    char timestr[64];
    char buffer[2048];
    char log_line[2200];
    time(&now);
    struct tm *tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);   // 날짜/시간 형식 문자열 생성
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);                    // 가변 인자 포함 본문 메시지 생성
    va_end(args);
    int len = snprintf(log_line, sizeof(log_line), "[%s] [%s] [PID:%d] %s\n", timestr, log_level_string(level), getpid(), buffer);  // 날짜, 레벨, PID 포함 최종 로그 생성
    if (len < 0)
        len = 0;
    else if (len >= (int)sizeof(log_line))
        len = sizeof(log_line) - 1;
    write(STDOUT_FILENO, log_line, len);                                // 표준 출력(콘솔)에 로그 출력
    if (state && state->log_fd >= 0)                                    // 로그 파일이 열려 있다면
        write(state->log_fd, log_line, len);                            // 파일에도 로그 기록
}
void 
log_init(ServerState *state)
{
    if (state == NULL)
        return;
    state->log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);        // 로그 파일 생성 또는 추가 모드로 열기
    if (state->log_fd == -1) 
    {
        fprintf(stderr, "log_init() : 로그 파일 열기 실패: %s\n", strerror(errno));
        return;
    }
    int flags = fcntl(state->log_fd, F_GETFD);                                  // 파일 디스크립터 플래그 읽기
    if (flags != -1) 
    {
        if (fcntl(state->log_fd, F_SETFD, flags | FD_CLOEXEC) == -1)            // exec 시 이 파일이 닫히도록 설정
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
