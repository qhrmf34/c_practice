#include "server_function.h"
void 
shutdown_workers(ServerState *state)
{
    time_t end_time = time(NULL);
    log_message(state, LOG_INFO, "서버 종료 시그널 수신");
    log_message(state, LOG_INFO, "총 실행 시간: %ld초", end_time - state->start_time);
    log_message(state, LOG_INFO, "성공한 fork: %d개", state->total_forks);
    log_message(state, LOG_INFO, "회수한 좀비: %d개", state->zombie_reaped);
    int initial_count = state->worker_count;                                                                // 종료 전 워커 수 기록
    if (initial_count > 0) 
    {
        log_message(state, LOG_INFO, "shutdown_workers() : Worker %d개에 SIGTERM 전송", initial_count);      // 자식들에게 종료 권고 신호 전송
        if (kill(0, SIGTERM) == -1)                                                                         // 그룹 내 모든 자식 프로세스에 신호 전송
            log_message(state, LOG_ERROR, "shutdown_workers() : kill(0, SIGTERM) 실패: %s", strerror(errno));
    }
    log_message(state, LOG_INFO, "Worker 정상 종료 대기 중 (최대 5초)...");
    time_t wait_start = time(NULL);                                                                         // 대기 시작 시간 기록
    while (time(NULL) - wait_start < 5)                                                                     // 최대 5초간 자식들의 자발적 종료 대기
    {
        handle_child_died(state);                                                                           // 종료된 자식 회수(waitpid)
        if (state->worker_count == 0)                                                                       // 모두 종료되었으면 즉시 반환
        {
            log_message(state, LOG_INFO, "모든 Worker 정상 종료 완료");
            return;
        }
    }
    int graceful_exits = initial_count - state->worker_count;
    log_message(state, LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개", graceful_exits, state->worker_count);
    if (state->worker_count > 0)                                                                            // 5초 후에도 살아있는 워커가 있다면
    {
        log_message(state, LOG_WARNING, "shutdown_workers() : 남은 Worker %d개 강제 종료 (SIGKILL)", state->worker_count);
        if (kill(0, SIGKILL) == -1)                                                                         // 강제 종료 신호 전송
            log_message(state, LOG_ERROR, "shutdown_workers() : kill(0, SIGKILL) 실패: %s", strerror(errno));
    }
}
void 
final_cleanup(ServerState *state)
{
    log_message(state, LOG_INFO, "최종 좀비 회수 중...");
    int final_count = 0;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        final_count++;
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
            log_message(state, LOG_DEBUG, "final_cleanup() : SIGKILL로 종료된 Worker 회수: PID %d", pid);
    }
    if (pid == -1 && errno != ECHILD)
        log_message(state, LOG_ERROR, "final_cleanup() : waitpid() 에러: %s", strerror(errno));
    log_message(state, LOG_INFO, "최종 회수: %d개", final_count);
    log_message(state, LOG_INFO, "총 회수된 좀비: %d개", state->zombie_reaped + final_count);
    log_message(state, LOG_INFO, "남은 활성 Worker: %d개", state->worker_count);
    if (state->worker_count > 0)
        log_message(state, LOG_WARNING, "경고: 회수하지 못한 Worker가 있을 수 있음");
    print_resource_limits();
    log_message(state, LOG_INFO, "=== 서버 정상 종료 완료 ===");
}