// ═══════════════════════════════════════════════════════════
// producer_kafka.c - Kafka로 메시지 전송
// ═══════════════════════════════════════════════════════════
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "queue.h"
#include "kafka_wrapper.h"
#include "kafka_config.h"

int main() {
    Queue *local_queue;
    KafkaProducer *kafka_producer;
    char buffer[100];
    int i = 0;
    
    printf("═══════════════════════════════════════════\n");
    printf("  Kafka Producer (PID: %d)\n", getpid());
    printf("═══════════════════════════════════════════\n\n");
    
    // 로컬 큐 연결 (선택사항 - 로컬 버퍼링용)
    local_queue = queue_attach();
    if (!local_queue) {
        fprintf(stderr, "로컬 큐 연결 실패 (계속 진행)\n");
    }
    
    // Kafka Producer 생성
    kafka_producer = kafka_producer_create(KAFKA_BROKERS);
    if (!kafka_producer) {
        fprintf(stderr, "Kafka Producer 생성 실패\n");
        exit(1);
    }
    
    // 메시지 생성 및 전송
    while (1) {
        sprintf(buffer, "Message-%d (PID:%d)", i, getpid());
        
        printf("[Producer] 메시지 생성: '%s'\n", buffer);
        
        // 1️⃣ 로컬 큐에 저장 (버퍼링)
        if (local_queue) {
            queue_enqueue(local_queue, buffer);
            printf("[Producer] 로컬 큐 저장 완료\n");
        }
        
        // 2️⃣ Kafka로 전송
        if (kafka_producer_send(kafka_producer, KAFKA_TOPIC, NULL, buffer) == 0) {
            printf("[Producer] Kafka 전송 완료\n");
        } else {
            fprintf(stderr, "[Producer] Kafka 전송 실패\n");
        }
        
        printf("[Producer] ────────────────────\n\n");
        
        i++;
        usleep(500000);  // 0.5초
    }
    
    // 정리
    kafka_producer_destroy(kafka_producer);
    if (local_queue) {
        queue_detach(local_queue);
    }
    
    return 0;
}