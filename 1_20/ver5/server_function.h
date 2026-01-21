#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_WORKERS 10000
#define LOG_FILE "server.log"
#define IO_TARGET 10
#define POLL_TIMEOUT 1000
#define SESSION_IDLE_TIMEOUT 60

typedef enum {
    SESSION_IDLE = 0,
    SESSION_ACTIVE,
    SESSION_CLOSING,
    SESSION_CLOSED
} SessionState;

typedef enum {
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING
} LogLevel;

typedef struct {
    int sock;
    struct sockaddr_in addr;
    int session_id;
    SessionState state;
    int io_count;
    time_t start_time;
    time_t last_activity;
} SessionDescriptor;

typedef struct {
    int active_sessions;
    int total_sessions;
    long heap_usage;
    int open_fds;
    time_t start_time;
} ResourceMonitor;

typedef struct {
    int running;
    pid_t worker_pids[MAX_WORKERS];
    int worker_count;
    int total_forks;
    int zombie_reaped;
    time_t start_time;
    pid_t parent_pid;
    int signal_pipe[2];
} ServerState;

void run_server(void);
int create_server_socket(void);
int accept_client(int serv_sock, struct sockaddr_in *clnt_addr);
int fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr, ServerState *state);
void handle_signal(ServerState *state, int signo);
void reap_children(ServerState *state);
void shutdown_all_workers(ServerState *state);
void child_process_main(int client_sock, int session_id, struct sockaddr_in client_addr);
void print_resource_limits(void);
void monitor_resources(ResourceMonitor *monitor);
void print_resource_status(ResourceMonitor *monitor);
long get_heap_usage(void);
int count_open_fds(void);
void log_message(LogLevel level, const char* format, ...);
void log_init(void);
void setup_signal_handlers(ServerState *state);
void setup_child_signal_handlers(void);

#endif
