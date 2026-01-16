#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <dirent.h>
#include <string.h>
#include <malloc.h>

// 힙 메모리 사용량 체크 (malloc_info 사용)
long
get_heap_usage(void)
{
#ifdef __GLIBC__
    struct mallinfo mi = mallinfo();
    // arena: 할당된 총 메모리 (bytes)
    // uordblks: 사용 중인 메모리 (bytes)
    return (long)mi.uordblks;
#else
    // mallinfo가 없는 시스템에서는 0 반환
    return 0;
#endif
}

// 열린 파일 디스크립터 수 체크
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
    {
        // /proc 접근 불가 시 -1 반환
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL)
    {
        // ".", ".." 제외
        if (entry->d_name[0] != '.')
        {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// 리소스 모니터링
void
monitor_resources(ResourceMonitor *monitor)
{
    if (monitor == NULL)
        return;
    
    // 힙 메모리 사용량
    monitor->heap_usage = get_heap_usage();
    
    // 열린 파일 디스크립터 수
    monitor->open_fds = count_open_fds();
}

// 리소스 상태 출력
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
    {
        printf("힙 메모리 사용: %ld bytes (%.2f KB)\n", 
               monitor->heap_usage, monitor->heap_usage / 1024.0);
    }
    else
    {
        printf("힙 메모리 사용: 측정 불가\n");
    }
    
    if (monitor->open_fds >= 0)
    {
        printf("열린 FD: %d개\n", monitor->open_fds);
    }
    else
    {
        printf("열린 FD: 측정 불가\n");
    }
    
    printf("=====================================\n\n");
}

// 시스템 리소스 한계 출력
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
    
    if (getrlimit(RLIMIT_AS, &rlim) == 0)
    {
        printf("최대 주소 공간: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한");
        else
            printf("%lu bytes", (unsigned long)rlim.rlim_cur);
        
        printf(" (hard: ");
        if (rlim.rlim_max == RLIM_INFINITY)
            printf("무제한)\n");
        else
            printf("%lu bytes)\n", (unsigned long)rlim.rlim_max);
    }
    
    printf("==========================\n");
}