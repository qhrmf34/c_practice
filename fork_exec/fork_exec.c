#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int 
main() 
{
    pid_t pid;
    int status;
    
    printf("=== Fork + Exec 예제 ===\n\n");
    
    // 첫 번째 자식: echo 명령 실행
    pid = fork();
    if (pid == 0) 
    {
        printf("[자식 1] echo 명령 실행\n");
        execlp("echo", "echo", "안녕하세요, fork+exec입니다!", NULL);
        exit(1);
    }
    wait(&status);
    
    // 두 번째 자식: 현재 디렉토리 파일 목록
    pid = fork();
    if (pid == 0) 
    {
        printf("\n[자식 2] 현재 디렉토리 목록\n");
        execlp("ls", "ls", "-la", NULL);
        exit(1);
    }
    wait(&status);
    
    // 세 번째 자식: 날짜 출력
    pid = fork();
    if (pid == 0) 
    {
        printf("\n[자식 3] 현재 날짜/시간\n");
        execlp("date", "date", NULL);
        exit(1);
    }
    wait(&status);
    
    printf("\n부모 프로세스: 모든 자식 프로세스 완료\n");
    
    return 0;
}