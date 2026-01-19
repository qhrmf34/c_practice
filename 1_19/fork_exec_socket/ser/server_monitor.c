#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <dirent.h>
#include <string.h>
#include <malloc.h>

// === 단일 프로세스 모니터링 함수들 ===

// 힙 메모리 사용량 체크 (malloc_info 사용)
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
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// 리소스 모니터링 (현재 프로세스)
void
monitor_resources(ResourceMonitor *monitor)
{
    if (monitor == NULL)
        return;
    
    monitor->heap_usage = get_heap_usage();
    monitor->open_fds = count_open_fds();
}

// 리소스 상태 출력 (현재 프로세스)
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


// === 전체 시스템 모니터링 함수들 (NEW!) ===

// 특정 PID의 메모리 사용량 읽기 (/proc/[PID]/status)
static long
get_process_memory(pid_t pid)
{
    char path[256];
    char line[256];
    FILE *fp;
    long vmrss = 0;
    
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    
    // VmRSS 찾기 (실제 물리 메모리 사용량)
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if (strncmp(line, "VmRSS:", 6) == 0)
        {
            sscanf(line + 6, "%ld", &vmrss);
            break;
        }
    }
    
    fclose(fp);
    return vmrss * 1024;  // KB → bytes
}

// 특정 PID의 FD 개수 세기
static int
count_process_fds(pid_t pid)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    char fd_path[256];
    
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);
    
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

// worker 프로세스 찾기 (pgrep worker 대신 /proc 스캔)
static int
find_worker_pids(pid_t *pids, int max_count)
{
    DIR *proc_dir;
    struct dirent *entry;
    int count = 0;
    
    proc_dir = opendir("/proc");
    if (proc_dir == NULL)
        return -1;
    
    while ((entry = readdir(proc_dir)) != NULL && count < max_count)
    {
        // 숫자 디렉토리만 (PID) - d_type 대신 이름으로 판단
        if (entry->d_name[0] >= '1' && entry->d_name[0] <= '9')
        {
            // 모든 문자가 숫자인지 확인
            int is_numeric = 1;
            for (int i = 0; entry->d_name[i]; i++)
            {
                if (entry->d_name[i] < '0' || entry->d_name[i] > '9')
                {
                    is_numeric = 0;
                    break;
                }
            }
            
            if (!is_numeric)
                continue;
            
            pid_t pid = atoi(entry->d_name);
            char cmdline_path[256];
            char cmdline[256];
            FILE *fp;
            
            // /proc/[PID]/cmdline 읽기
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
            fp = fopen(cmdline_path, "r");
            if (fp != NULL)
            {
                if (fgets(cmdline, sizeof(cmdline), fp) != NULL)
                {
                    // "worker" 문자열 포함 여부 확인
                    if (strstr(cmdline, "worker") != NULL)
                    {
                        pids[count++] = pid;
                    }
                }
                fclose(fp);
            }
        }
    }
    
    closedir(proc_dir);
    return count;
}

// 전체 시스템 통계 출력
void
print_system_status(void)
{
    pid_t worker_pids[256];
    int worker_count;
    long total_memory = 0;
    int total_fds = 0;
    int i;
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║           전체 시스템 모니터링 (서버 + Worker)           ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    // 서버 프로세스 정보
    pid_t server_pid = getpid();
    long server_memory = get_process_memory(server_pid);
    int server_fds = count_process_fds(server_pid);
    
    printf(" 서버 프로세스 (PID: %d)\n", server_pid);
    printf("   ├─ 메모리: %.2f KB\n", server_memory / 1024.0);
    printf("   └─ FD: %d개\n\n", server_fds);
    
    total_memory += server_memory;
    total_fds += server_fds;
    
    // Worker 프로세스들 찾기
    worker_count = find_worker_pids(worker_pids, 256);
    
    if (worker_count > 0)
    {
        printf("Worker 프로세스들 (%d개)\n", worker_count);
        
        for (i = 0; i < worker_count; i++)
        {
            long mem = get_process_memory(worker_pids[i]);
            int fds = count_process_fds(worker_pids[i]);
            
            if (mem >= 0 && fds >= 0)
            {
                printf("   ├─ Worker #%d (PID: %d)\n", i+1, worker_pids[i]);
                printf("   │  ├─ 메모리: %.2f KB\n", mem / 1024.0);
                printf("   │  └─ FD: %d개\n", fds);
                
                total_memory += mem;
                total_fds += fds;
            }
        }
        printf("\n");
    }
    else
    {
        printf("Worker 프로세스: 없음\n\n");
    }
    
    // 전체 합계
    printf("전체 합계\n");
    printf("   ├─ 총 프로세스: %d개 (서버 1 + Worker %d)\n", 
           1 + worker_count, worker_count);
    printf("   ├─ 총 메모리: %.2f KB (%.2f MB)\n", 
           total_memory / 1024.0, total_memory / 1024.0 / 1024.0);
    printf("   └─ 총 FD: %d개\n", total_fds);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
}

// 간단 버전: worker 개수만 출력
void
print_simple_status(void)
{
    pid_t worker_pids[256];
    int worker_count = find_worker_pids(worker_pids, 256);
    
    printf("[시스템] 서버: 1개, Worker: %d개, 총: %d 프로세스\n", 
           worker_count, 1 + worker_count);
}


// === 기존 함수 (유지) ===

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