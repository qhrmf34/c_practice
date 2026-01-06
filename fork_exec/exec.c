#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    pid_t pid;
    
    printf("프로그램 시작\n");
    
    pid = fork();
    
    if (pid < 0) {
        perror("fork 실패");
        return 1;
    }
    else if (pid == 0) {
        // 자식 프로세스에서 ls 명령 실행
        printf("자식: ls 명령 실행\n");
        execlp("ls", "ls", "-l", NULL);
        
        // exec가 성공하면 이 아래 코드는 실행 안 됨
        perror("exec 실패");
        return 1;
    }
    else {
        // 부모 프로세스
        wait(NULL);
        printf("부모: 자식 프로세스 종료 확인\n");
    }
    
    return 0;
}