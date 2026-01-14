// ═══════════════════════════════════════════════════════════
// consumer.c - 소비자 (queue API만 사용)
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "queue.h"

int main() {
    Queue *q;
    char buffer[100];
    
    printf("  소비자 프로세스 (PID: %d)\n", getpid());
        
    // 큐에 연결
    q = queue_attach();
    if (q == NULL) {
        fprintf(stderr, "큐 연결 실패\n");
        exit(1);
    }
    
    // 데이터 소비
    while (1) {
        printf("[소비자] 데이터 대기 중...\n");
        
        if (queue_dequeue(q, buffer, sizeof(buffer)) == 0) {
            printf("[소비자] '%s' 추출 완료! (현재 개수: %d)\n", 
                   buffer, queue_count(q));
            printf("[소비자] 처리 완료!\n\n");
        } else {
            fprintf(stderr, "[소비자] 추출 실패\n");
        }
        
        usleep(300000);  // 0.3초
    }
    
    queue_detach(q);
    return 0;
}