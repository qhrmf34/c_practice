#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int 
main() 
{
    pid_t pid;
    printf("부모 프로세스 시작 (PID: %d)\n", getpid());

    pid = fork();
    
    if (pid < 0) 
    {
        // fork 실패
        perror("fork 실패");
        return 1;
    }
    else if (pid == 0) 
    {
        // 자식 프로세스
        printf("자식 프로세스 (PID: %d, 부모 PID: %d)\n", 
               getpid(), getppid());
        sleep(2);
        printf("자식 프로세스 종료\n");
    }
    else 
    {
        // 부모 프로세스
        printf("부모 프로세스: 자식 PID = %d\n", pid);
        wait(NULL);  // 자식 프로세스 종료 대기
        printf("부모 프로세스 종료\n");
    }
    
    return 0;
}