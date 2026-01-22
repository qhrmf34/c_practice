#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <dirent.h>
#include <string.h>
#include <malloc.h>

long
get_heap_usage(void)
{
#ifdef __GLIBC__
    struct mallinfo2 mi = mallinfo2();                                           /* glibc의 메모리 할당 정보 조회 */
    return (long)mi.uordblks;                                                    /* uordblks: 사용 중인 힙 바이트 */
#else
    return 0;                                                                    /* glibc 아닌 경우 측정 불가 */
#endif
}

int
count_open_fds(void)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    char fd_path[256];
    
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", getpid());                /* /proc/PID/fd 디렉토리 경로 */
    dir = opendir(fd_path);                                                      /* 디렉토리 열기 */
    if (dir == NULL)                                                             /* opendir 실패: /proc 없음, 권한 없음, PID 없음 */
        return -1;
    
    while ((entry = readdir(dir)) != NULL)                                       /* 모든 엔트리 순회 */
    {
        if (entry->d_name[0] != '.')                                             /* '.'과 '..' 제외 */
            count++;
    }
    closedir(dir);
    return count;
}

void
monitor_resources(ResourceMonitor *monitor)
{
    if (monitor == NULL)                                                         /* NULL 포인터 방어 */
        return;
    monitor->heap_usage = get_heap_usage();
    monitor->open_fds = count_open_fds();
}

void
print_resource_status(ResourceMonitor *monitor)
{
    if (monitor == NULL)                                                         /* NULL 포인터 방어 */
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
    
    if (getrlimit(RLIMIT_NPROC, &rlim) == 0)                                     /* 최대 프로세스 수 조회 */
    {
        printf("최대 프로세스 수: ");
        if (rlim.rlim_cur == RLIM_INFINITY)                                      /* 무제한 */
            printf("무제한");
        else
            printf("%lu", (unsigned long)rlim.rlim_cur);
        printf(" (hard: ");
        if (rlim.rlim_max == RLIM_INFINITY)
            printf("무제한)\n");
        else
            printf("%lu)\n", (unsigned long)rlim.rlim_max);
    }
    
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)                                    /* 최대 파일 디스크립터 조회 */
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
