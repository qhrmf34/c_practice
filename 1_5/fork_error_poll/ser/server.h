#define _POSIX_C_SOURCE 200809L
#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <poll.h>

/* 설정 */
#define SERVER_PORT     9190
#define BUFFER_SIZE     4096
#define LISTEN_BACKLOG  128

/* 리소스 모니터링 */
typedef struct 
{
    long malloc_count;
    long free_count;
    long malloc_bytes;
    long free_bytes;
    long current_bytes;
    pthread_mutex_t lock;
} ResourceMonitor;

/* 전역 변수 */
extern ResourceMonitor g_monitor;
extern volatile sig_atomic_t g_server_running;
extern int g_use_waitpid;

/* signal_handler.c */
void 
setup_signal_handlers(void);
void 
print_server_statistics(void);

/* parent_process.c */
int 
create_server_socket(int port);
void 
run_parent_process(int server_sock);

/* child_process.c */
void 
run_child_process(int client_sock);

/* resource_monitor.c */
void 
init_resource_monitor(void);

void* 
tracked_malloc(size_t size, const char* caller);

void 
tracked_free(void* ptr, size_t size, const char* caller);

void 
print_resource_status(const char* label);

int 
get_fd_count(void);

int 
get_zombie_count(void);

void 
log_info(const char* format, ...);

void 
error_exit(const char* msg);

/* signal_handler.c */
void 
increment_fork_count(void);
int 
get_fork_count(void);
void 
print_server_statistics(void);


#endif