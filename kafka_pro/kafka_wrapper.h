// ═══════════════════════════════════════════════════════════
// kafka_wrapper.h - Kafka 래퍼 인터페이스
// ═══════════════════════════════════════════════════════════
#ifndef KAFKA_WRAPPER_H
#define KAFKA_WRAPPER_H

#include <stddef.h>

// 불투명 포인터
typedef struct KafkaProducer KafkaProducer;
typedef struct KafkaConsumer KafkaConsumer;

// ═══════════════════════════════════════════════════════════
// Producer API
// ═══════════════════════════════════════════════════════════

/**
 * Kafka Producer 생성
 * @param brokers 브로커 주소 (예: "localhost:9092")
 * @return Producer 포인터, 실패 시 NULL
 */
KafkaProducer* kafka_producer_create(const char *brokers);

/**
 * 메시지 전송
 * @param producer Producer 포인터
 * @param topic 토픽 이름
 * @param key 메시지 키 (NULL 가능)
 * @param data 메시지 데이터
 * @return 성공 0, 실패 -1
 */
int kafka_producer_send(KafkaProducer *producer, 
                       const char *topic,
                       const char *key,
                       const char *data);

/**
 * Producer 정리
 * @param producer Producer 포인터
 */
void kafka_producer_destroy(KafkaProducer *producer);

// ═══════════════════════════════════════════════════════════
// Consumer API
// ═══════════════════════════════════════════════════════════

/**
 * Kafka Consumer 생성
 * @param brokers 브로커 주소
 * @param group_id Consumer 그룹 ID
 * @param topic 구독할 토픽
 * @return Consumer 포인터, 실패 시 NULL
 */
KafkaConsumer* kafka_consumer_create(const char *brokers,
                                    const char *group_id,
                                    const char *topic);

/**
 * 메시지 수신 (블로킹)
 * @param consumer Consumer 포인터
 * @param buffer 데이터를 저장할 버퍼
 * @param size 버퍼 크기
 * @param timeout_ms 타임아웃 (밀리초)
 * @return 성공 0, 타임아웃 1, 실패 -1
 */
int kafka_consumer_receive(KafkaConsumer *consumer,
                          char *buffer,
                          size_t size,
                          int timeout_ms);

/**
 * Consumer 정리
 * @param consumer Consumer 포인터
 */
void kafka_consumer_destroy(KafkaConsumer *consumer);

#endif