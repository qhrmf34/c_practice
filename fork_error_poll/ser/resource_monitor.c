#include "server.h"
#include <stdarg.h>
#include <dirent.h>

ResourceMonitor g_monitor;

void
init_resource_monitor(void)
{
    memset(&g_monitor, 0, sizeof(ResourceMonitor));
    pthread_mutex_init(&g_monitor.lock, NULL);
    log_info("[모니터] 리소스 모니터링 시작");
}

void*
tracked_malloc(size_t size, const char* caller)
{
    void* ptr = malloc(size);
    
    if (ptr) {
        pthread_mutex_lock(&g_monitor.lock);
        g_monitor.malloc_count++;
        g_monitor.malloc_bytes += size;
        g_monitor.current_bytes += size;
        pthread_mutex_unlock(&g_monitor.lock);

    }
    
    return ptr;
}

void
tracked_free(void* ptr, size_t size, const char* caller)
{
    if (ptr) {
        pthread_mutex_lock(&g_monitor.lock);
        g_monitor.free_count++;
        g_monitor.free_bytes += size;
        g_monitor.current_bytes -= size;
        pthread_mutex_unlock(&g_monitor.lock);
        
        log_info("[메모리] free(%zu) by %s -> 현재: %ld bytes", 
                 size, caller, g_monitor.current_bytes);
        
        free(ptr);
    }
}

int
get_fd_count(void)
{
    int count = 0;
    DIR* dir = opendir("/proc/self/fd");
    
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                count++;
            }
        }
        closedir(dir);
    }
    
    return count;
}

int
get_zombie_count(void)
{
    FILE* fp = popen("ps -eo stat | grep -c Z 2>/dev/null", "r");
    int count = 0;
    
    if (fp) {
        if (fscanf(fp, "%d", &count) != 1) {
            count = 0;
        }
        pclose(fp);
    }
    
    return count;
}

void
print_resource_status(const char* label)
{
    pthread_mutex_lock(&g_monitor.lock);
    
    printf("\n");
    printf("==================== 리소스 상태: %s ====================\n", label);
    printf("[메모리 통계]\n");
    printf("  malloc 호출:      %ld 회\n", g_monitor.malloc_count);
    printf("  free 호출:        %ld 회\n", g_monitor.free_count);
    printf("  메모리 누수 의심: %ld 회\n", 
           g_monitor.malloc_count - g_monitor.free_count);
    printf("  총 할당:          %ld bytes\n", g_monitor.malloc_bytes);
    printf("  총 해제:          %ld bytes\n", g_monitor.free_bytes);
    printf("  현재 사용:        %ld bytes\n", g_monitor.current_bytes);
    
    printf("\n[시스템 리소스]\n");
    printf("  열린 FD 개수:     %d 개\n", get_fd_count());
    printf("  좀비 프로세스:    %d 개\n", get_zombie_count());
    
    printf("=================================================================\n\n");
    
    pthread_mutex_unlock(&g_monitor.lock);
}

void
log_info(const char* format, ...)
{
    va_list args;
    time_t now;
    char timestr[64];
    
    time(&now);
    strftime(timestr, sizeof(timestr), "%H:%M:%S", localtime(&now));
    
    printf("[%s] ", timestr);
    
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}

void
error_exit(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}