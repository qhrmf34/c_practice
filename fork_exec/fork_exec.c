#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    pid_t pid;
    int status;

    printf("=== Fork + Exec 계열 테스트 ===\n\n");

    // 1️⃣ execl - 리스트 + 경로 직접 지정
    pid = fork();
    if (pid == 0) {
        printf("[자식 1] execl로 echo 실행\n");
        execl("/bin/echo", "echo", "안녕하세요, execl입니다!", NULL);
        perror("execl 실패");
        exit(1);
    }
    wait(&status);

    // 2️⃣ execlp - 리스트 + PATH 검색
    pid = fork();
    if (pid == 0) {
        printf("\n[자식 2] execlp로 ls 목록 출력\n");
        execlp("ls", "ls", "-la", NULL);
        perror("execlp 실패");
        exit(1);
    }
    wait(&status);

    // 3️⃣ execv - 배열 + 경로 직접 지정
    pid = fork();
    if (pid == 0) {
        printf("\n[자식 3] execv로 date 출력\n");
        char *args[] = {"date", NULL};
        execv("/bin/date", args);
        perror("execv 실패");
        exit(1);
    }
    wait(&status);

    // 4️⃣ execvp - 배열 + PATH 검색
    pid = fork();
    if (pid == 0) {
        printf("\n[자식 4] execvp로 ls 출력\n");
        char *args[] = {"ls", "-l", NULL};
        execvp("ls", args);
        perror("execvp 실패");
        exit(1);
    }
    wait(&status);

    printf("\n부모 프로세스: 모든 자식 프로세스 완료\n");

    return 0;
}
