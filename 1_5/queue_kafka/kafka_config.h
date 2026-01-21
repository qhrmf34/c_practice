// ═══════════════════════════════════════════════════════════
// kafka_config.h - Kafka 설정
// ═══════════════════════════════════════════════════════════
#ifndef KAFKA_CONFIG_H
#define KAFKA_CONFIG_H

// Kafka 브로커 주소
#define KAFKA_BROKERS "localhost:9092"

// Kafka 토픽 이름
#define KAFKA_TOPIC "message-queue"

// Consumer 그룹 ID
#define KAFKA_GROUP_ID "consumer-group-1"

#endif