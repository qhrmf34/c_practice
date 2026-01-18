// ═══════════════════════════════════════════════════════════
// main.c - 메인 프로세스
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "queue.h"

// 전역 변수
Queue *g_queue = NULL;
pid_t producer_pid = -1;
pid_t consumer_pid = -1;

// 시그널 핸들러 (Ctrl+C 처리)
void 
signal_handler(int signo) 
{
    printf("\n\n[메인]종료 시그널 %d 받음.정리 중...\n",signo);
    
    // 자식 프로세스 종료
    if (producer_pid > 0) 
    {
        kill(producer_pid, SIGTERM);
    }
    if (consumer_pid > 0) 
    {
        kill(consumer_pid, SIGTERM);
    }
    
    // 큐 정리
    if (g_queue != NULL) 
    {
        queue_destroy(g_queue);
    }
    
    exit(0);
}

int 
main() 
{
    printf("═══════════════════════════════════════════\n");
    printf("  실무 스타일 생산자-소비자\n");
    printf("  메인 PID: %d\n", getpid());
    printf("═══════════════════════════════════════════\n\n");
    
    // 시그널 핸들러 등록
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill
    
    // 큐 생성
    g_queue = queue_create();
    if (g_queue == NULL) 
    {
        fprintf(stderr, "큐 생성 실패\n");
        exit(1);
    }
    
    // 생산자 프로세스 생성
    producer_pid = fork();
    if (producer_pid < 0) 
    {
        perror("fork 실패");
        queue_destroy(g_queue);
        exit(1);
    }
    else if (producer_pid == 0) 
    {
        // 자식: 생산자 실행
        execl("./producer", "producer", NULL);
        perror("execl 실패");
        exit(1);
    }
    
    printf("[메인] 생산자 생성 (PID: %d)\n", producer_pid);
    
    // 소비자 프로세스 생성
    consumer_pid = fork();
    if (consumer_pid < 0) 
    {
        perror("fork 실패");
        kill(producer_pid, SIGTERM);
        queue_destroy(g_queue);
        exit(1);
    }
    else if (consumer_pid == 0) 
    {
        // 자식: 소비자 실행
        execl("./consumer", "consumer", NULL);
        perror("execl 실패");
        exit(1);
    }
    
    printf("[메인] 소비자 생성 (PID: %d)\n", consumer_pid);
    printf("[메인] 실행 중... (Ctrl+C로 종료)\n\n");
    
    // 자식 프로세스 종료 대기
    int status;
    pid_t pid = wait(&status);
    printf("[메인] 프로세스 %d 종료\n", pid);
    
    pid = wait(&status);
    printf("[메인] 프로세스 %d 종료\n", pid);
    
    // 정리
    queue_destroy(g_queue);
    printf("[메인] 프로그램 종료\n");
    
    return 0;
}