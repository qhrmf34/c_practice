#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <sys/resource.h>

// 시스템 리소스 출력
void
print_resource_limits(void)
{
    struct rlimit rlim;
    
    if (getrlimit(RLIMIT_NPROC, &rlim) == 0)
    {
        printf("[리소스] 최대 프로세스 수: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한\n");
        else
            printf("%lu (hard: %lu)\n", rlim.rlim_cur, rlim.rlim_max);
    }
    
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        printf("[리소스] 최대 파일 디스크립터: ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("무제한\n");
        else
            printf("%lu (hard: %lu)\n", rlim.rlim_cur, rlim.rlim_max);
    }
}
