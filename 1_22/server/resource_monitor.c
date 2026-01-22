#include "server_function.h"
#include <sys/resource.h>
#include <dirent.h>
#include <malloc.h>

long 
get_heap_usage(void)
{
#ifdef __GLIBC__
    struct mallinfo2 mi = mallinfo2();
    return (long)mi.uordblks;
#else
    return 0;
#endif
}
int 
count_open_fds(void)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    char fd_path[256];
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", getpid());
    dir = opendir(fd_path);
    if (dir == NULL)
        return -1;
    while ((entry = readdir(dir)) != NULL) 
    {
        if (entry->d_name[0] != '.')
            count++;
    }
    closedir(dir);
    return count;
}
void 
monitor_resources(ResourceMonitor *monitor)
{
    if (monitor == NULL)
        return;
    monitor->heap_usage = get_heap_usage();
    monitor->open_fds = count_open_fds();
}
void 
print_resource_status(ResourceMonitor *monitor)
{
    if (monitor == NULL)
        return;
    time_t current_time = time(NULL);
    time_t elapsed = current_time - monitor->start_time;
    printf("\n=== 리소스 모니터링 (PID: %d) ===\n", getpid());
    printf("실행 시간: %ld초\n", elapsed);
    printf("활성 세션: %d개\n", monitor->active_sessions);
    printf("총 처리 세션: %d개\n", monitor->total_sessions);
    if (monitor->heap_usage >= 0)
        printf("힙 메모리 사용: %ld bytes (%.2f KB)\n", monitor->heap_usage, monitor->heap_usage / 1024.0);
    else
        printf("힙 메모리 사용: 측정 불가\n");
    if (monitor->open_fds >= 0)
        printf("열린 FD: %d개\n", monitor->open_fds);
    else
        printf("열린 FD: 측정 불가\n");
}
void 
print_resource_limits(void)
{
    struct rlimit rlim;
    printf("\n=== 시스템 리소스 한계 ===\n");
    if (getrlimit(RLIMIT_NPROC, &rlim) == 0) 
    {
        printf("최대 프로세스 수: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한");
        else
            printf("%lu", (unsigned long)rlim.rlim_cur);
        printf(" (hard: ");
        if (rlim.rlim_max == RLIM_INFINITY)
            printf("무제한)\n");
        else
            printf("%lu)\n", (unsigned long)rlim.rlim_max);
    }
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) 
    {
        printf("최대 파일 디스크립터: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한");
        else
            printf("%lu", (unsigned long)rlim.rlim_cur);
        printf(" (hard: ");
        if (rlim.rlim_max == RLIM_INFINITY)
            printf("무제한)\n");
        else
            printf("%lu)\n", (unsigned long)rlim.rlim_max);
    }
}
