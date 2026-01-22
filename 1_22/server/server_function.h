#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif
#define BUF_SIZE 1024
#define PORT 9190
#define MAX_WORKERS 10000
#define LOG_FILE "server.log"
#define IO_TARGET 10
#define POLL_TIMEOUT 1000
#define SESSION_IDLE_TIMEOUT 60
#define MAX_FRAMES 64
typedef enum 
{
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;
typedef enum 
{
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;
typedef struct 
{
    int sock;
    struct sockaddr_in addr;
    int session_id;
    SessionState state;
    int io_count;
    time_t start_time;
    time_t last_activity;
} SessionDescriptor;
typedef struct 
{
    int active_sessions;
    int total_sessions;
    long heap_usage;
    int open_fds;
    time_t start_time;
} ResourceMonitor;
typedef struct 
{
    volatile sig_atomic_t running;
    volatile sig_atomic_t child_died;
    int worker_count;
    int total_forks;
    int zombie_reaped;
    time_t start_time;
    pid_t parent_pid;
    int log_fd;
} ServerState;
extern void             run_server(void);
extern int              fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);
extern void             handle_child_died(ServerState *state);
extern void             shutdown_workers(ServerState *state);
extern void             final_cleanup(ServerState *state);
extern void             child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr, ServerState *state);
extern void             print_resource_limits(void);
extern void             monitor_resources(ResourceMonitor *monitor);
extern void             print_resource_status(ResourceMonitor *monitor);
extern long             get_heap_usage(void);
extern int              count_open_fds(void);
extern void             log_message(ServerState *state, LogLevel level, const char* format, ...);
extern void             log_init(ServerState *state);
extern void             log_close(ServerState *state);
extern void             setup_signal_handlers(ServerState *state);
extern void             setup_child_signal_handlers(ServerState *state);
extern void             test_segfault(void);
extern void             test_abort(void);
extern void             test_division_by_zero(void);
extern void             test_crash_with_stack(void);
#ifdef __cplusplus
}
#endif
#endif