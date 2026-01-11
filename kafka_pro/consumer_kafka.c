// ═══════════════════════════════════════════════════════════
// consumer_kafka.c - Kafka에서 메시지 수신
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "queue.h"
#include "kafka_wrapper.h"
#include "kafka_config.h"

int main() {
    Queue *local_queue;
    KafkaConsumer *kafka_consumer;
    char buffer[100];
    
    printf("═══════════════════════════════════════════\n");
    printf("  Kafka Consumer (PID: %d)\n", getpid());
    printf("═══════════════════════════════════════════\n\n");
    
    // 로컬 큐 연결 (선택사항)
    local_queue = queue_attach();
    if (!local_queue) {
        fprintf(stderr, "로컬 큐 연결 실패 (계속 진행)\n");
    }
    
    // Kafka Consumer 생성
    kafka_consumer = kafka_consumer_create(KAFKA_BROKERS,
                                          KAFKA_GROUP_ID,
                                          KAFKA_TOPIC);
    if (!kafka_consumer) {
        fprintf(stderr, "Kafka Consumer 생성 실패\n");
        exit(1);
    }
    
    // 메시지 수신 및 처리
    while (1) {
        printf("[Consumer] Kafka 메시지 대기 중...\n");
        
        // Kafka에서 메시지 수신
        int ret = kafka_consumer_receive(kafka_consumer, buffer,
                                        sizeof(buffer), 1000);
        
        if (ret == 0) {
            // 메시지 수신 성공
            printf("[Consumer] Kafka 수신: '%s'\n", buffer);
            
            // 로컬 큐에 저장 (다른 프로세스용)
            if (local_queue) {
                queue_enqueue(local_queue, buffer);
                printf("[Consumer] 로컬 큐 저장 완료\n");
            }
            
            // 메시지 처리
            printf("[Consumer] 처리 완료!\n");
            printf("[Consumer] ────────────────────\n\n");
            
        } else if (ret == 1) {
            // 타임아웃
            printf("[Consumer] 타임아웃 (메시지 없음)\n\n");
        } else {
            // 에러
            fprintf(stderr, "[Consumer] 수신 에러\n\n");
        }
    }
    
    // 정리
    kafka_consumer_destroy(kafka_consumer);
    if (local_queue) {
        queue_detach(local_queue);
    }
    
    return 0;
}