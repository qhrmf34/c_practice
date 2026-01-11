// ═══════════════════════════════════════════════════════════
// main_kafka.c - Kafka 시스템 메인
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "queue.h"

Queue *g_queue = NULL;
pid_t producer_pid = -1;
pid_t consumer_pid = -1;

void signal_handler(int signo) {
    printf("\n\n[메인] 종료 신호. 정리 중...\n");
    
    if (producer_pid > 0) kill(producer_pid, SIGTERM);
    if (consumer_pid > 0) kill(consumer_pid, SIGTERM);
    
    if (g_queue) queue_destroy(g_queue);
    
    exit(0);
}

int main() {
    printf("═══════════════════════════════════════════\n");
    printf("  Kafka 통합 메시징 시스템\n");
    printf("  메인 PID: %d\n", getpid());
    printf("═══════════════════════════════════════════\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 로컬 큐 생성 (버퍼용)
    g_queue = queue_create();
    if (!g_queue) {
        fprintf(stderr, "큐 생성 실패\n");
        exit(1);
    }
    
    // Kafka Producer 프로세스
    producer_pid = fork();
    if (producer_pid == 0) {
        execl("./producer_kafka", "producer_kafka", NULL);
        perror("execl 실패");
        exit(1);
    }
    printf("[메인] Kafka Producer 시작 (PID: %d)\n", producer_pid);
    
    sleep(2);  // Kafka 초기화 대기
    
    // Kafka Consumer 프로세스
    consumer_pid = fork();
    if (consumer_pid == 0) {
        execl("./consumer_kafka", "consumer_kafka", NULL);
        perror("execl 실패");
        exit(1);
    }
    printf("[메인] Kafka Consumer 시작 (PID: %d)\n", consumer_pid);
    
    printf("[메인] 실행 중... (Ctrl+C로 종료)\n\n");
    
    // 자식 프로세스 대기
    wait(NULL);
    wait(NULL);
    
    queue_destroy(g_queue);
    printf("[메인] 시스템 종료\n");
    
    return 0;
}