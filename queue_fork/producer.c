// ═══════════════════════════════════════════════════════════
// producer.c - 생산자 (queue API만 사용)
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "queue.h"

int main() {
    Queue *q;
    char buffer[100];
    int i = 0;
    
    printf("  생산자 프로세스 (PID: %d)\n", getpid());
    
    // 큐에 연결
    q = queue_attach();
    if (q == NULL) {
        fprintf(stderr, "큐 연결 실패\n");
        exit(1);
    }
    
    // 데이터 생산
    while (1) {
        sprintf(buffer, "Data-%d (PID:%d)", i, getpid());
        
        printf("[생산자] '%s' 삽입 시도...\n", buffer);
        
        if (queue_enqueue(q, buffer) == 0) {
            printf("[생산자] 삽입 완료! (현재 개수: %d)\n\n", 
                   queue_count(q));
        } else {
            fprintf(stderr, "[생산자] 삽입 실패\n");
        }
        
        i++;
        usleep(100000);  // 0.1초
    }
    
    queue_detach(q);
    return 0;
}