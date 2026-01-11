// ═══════════════════════════════════════════════════════════
// kafka_wrapper.c - Kafka 래퍼 구현
// ═══════════════════════════════════════════════════════════
#include "kafka_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librdkafka/rdkafka.h>

// ═══════════════════════════════════════════════════════════
// Producer 구조체
// ═══════════════════════════════════════════════════════════
struct KafkaProducer {
    rd_kafka_t *rk;           // Kafka 핸들
    rd_kafka_conf_t *conf;    // 설정
};

// ═══════════════════════════════════════════════════════════
// Consumer 구조체
// ═══════════════════════════════════════════════════════════
struct KafkaConsumer {
    rd_kafka_t *rk;              // Kafka 핸들
    rd_kafka_conf_t *conf;       // 설정
    rd_kafka_topic_partition_list_t *topics;  // 구독 토픽
};

// ═══════════════════════════════════════════════════════════
// Producer 생성
// ═══════════════════════════════════════════════════════════
KafkaProducer* kafka_producer_create(const char *brokers) {
    KafkaProducer *producer;
    char errstr[512];
    
    producer = (KafkaProducer*)malloc(sizeof(KafkaProducer));
    if (!producer) return NULL;
    
    // Kafka 설정 생성
    producer->conf = rd_kafka_conf_new();
    
    // 브로커 설정
    if (rd_kafka_conf_set(producer->conf, "bootstrap.servers", brokers,
                         errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "Kafka 설정 실패: %s\n", errstr);
        free(producer);
        return NULL;
    }
    
    // Producer 생성
    producer->rk = rd_kafka_new(RD_KAFKA_PRODUCER, producer->conf,
                               errstr, sizeof(errstr));
    if (!producer->rk) {
        fprintf(stderr, "Kafka Producer 생성 실패: %s\n", errstr);
        free(producer);
        return NULL;
    }
    
    printf("[Kafka] Producer 생성 완료 (brokers: %s)\n", brokers);
    return producer;
}

// ═══════════════════════════════════════════════════════════
// 메시지 전송
// ═══════════════════════════════════════════════════════════
int kafka_producer_send(KafkaProducer *producer,
                       const char *topic,
                       const char *key,
                       const char *data) {
    if (!producer || !topic || !data) return -1;
    
    size_t key_len = key ? strlen(key) : 0;
    size_t data_len = strlen(data);
    
    // 메시지 전송 (비동기)
    if (rd_kafka_produce(
            rd_kafka_topic_new(producer->rk, topic, NULL),  // 토픽
            RD_KAFKA_PARTITION_UA,     // 파티션 자동 선택
            RD_KAFKA_MSG_F_COPY,       // 데이터 복사
            (void*)data, data_len,     // 메시지
            (void*)key, key_len,       // 키
            NULL                       // 메시지별 opaque
        ) == -1) {
        fprintf(stderr, "Kafka 전송 실패: %s\n",
                rd_kafka_err2str(rd_kafka_last_error()));
        return -1;
    }
    
    // 전송 대기 (버퍼 flush)
    rd_kafka_poll(producer->rk, 0);
    
    return 0;
}

// ═══════════════════════════════════════════════════════════
// Producer 정리
// ═══════════════════════════════════════════════════════════
void kafka_producer_destroy(KafkaProducer *producer) {
    if (!producer) return;
    
    // 남은 메시지 전송 대기
    rd_kafka_flush(producer->rk, 10000);  // 10초 타임아웃
    
    rd_kafka_destroy(producer->rk);
    free(producer);
    printf("[Kafka] Producer 정리 완료\n");
}

// ═══════════════════════════════════════════════════════════
// Consumer 생성
// ═══════════════════════════════════════════════════════════
KafkaConsumer* kafka_consumer_create(const char *brokers,
                                    const char *group_id,
                                    const char *topic) {
    KafkaConsumer *consumer;
    char errstr[512];
    
    consumer = (KafkaConsumer*)malloc(sizeof(KafkaConsumer));
    if (!consumer) return NULL;
    
    // Kafka 설정 생성
    consumer->conf = rd_kafka_conf_new();
    
    // 브로커 설정
    rd_kafka_conf_set(consumer->conf, "bootstrap.servers", brokers,
                     errstr, sizeof(errstr));
    
    // Consumer 그룹 ID 설정
    rd_kafka_conf_set(consumer->conf, "group.id", group_id,
                     errstr, sizeof(errstr));
    
    // 자동 커밋 설정
    rd_kafka_conf_set(consumer->conf, "enable.auto.commit", "true",
                     errstr, sizeof(errstr));
    
    // Consumer 생성
    consumer->rk = rd_kafka_new(RD_KAFKA_CONSUMER, consumer->conf,
                               errstr, sizeof(errstr));
    if (!consumer->rk) {
        fprintf(stderr, "Kafka Consumer 생성 실패: %s\n", errstr);
        free(consumer);
        return NULL;
    }
    
    // 토픽 구독
    consumer->topics = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(consumer->topics, topic,
                                     RD_KAFKA_PARTITION_UA);
    
    if (rd_kafka_subscribe(consumer->rk, consumer->topics) != RD_KAFKA_RESP_ERR_NO_ERROR) {
        fprintf(stderr, "토픽 구독 실패\n");
        kafka_consumer_destroy(consumer);
        return NULL;
    }
    
    printf("[Kafka] Consumer 생성 완료 (group: %s, topic: %s)\n",
           group_id, topic);
    return consumer;
}

// ═══════════════════════════════════════════════════════════
// 메시지 수신
// ═══════════════════════════════════════════════════════════
int kafka_consumer_receive(KafkaConsumer *consumer,
                          char *buffer,
                          size_t size,
                          int timeout_ms) {
    if (!consumer || !buffer) return -1;
    
    // 메시지 poll
    rd_kafka_message_t *rkmessage;
    rkmessage = rd_kafka_consumer_poll(consumer->rk, timeout_ms);
    
    if (!rkmessage) {
        return 1;  // 타임아웃
    }
    
    if (rkmessage->err) {
        if (rkmessage->err != RD_KAFKA_RESP_ERR__PARTITION_EOF) {
            fprintf(stderr, "Consumer 에러: %s\n",
                    rd_kafka_message_errstr(rkmessage));
        }
        rd_kafka_message_destroy(rkmessage);
        return -1;
    }
    
    // 메시지 복사
    size_t copy_len = rkmessage->len < size - 1 ? rkmessage->len : size - 1;
    memcpy(buffer, rkmessage->payload, copy_len);
    buffer[copy_len] = '\0';
    
    rd_kafka_message_destroy(rkmessage);
    return 0;
}

// ═══════════════════════════════════════════════════════════
// Consumer 정리
// ═══════════════════════════════════════════════════════════
void kafka_consumer_destroy(KafkaConsumer *consumer) {
    if (!consumer) return;
    
    rd_kafka_consumer_close(consumer->rk);
    rd_kafka_topic_partition_list_destroy(consumer->topics);
    rd_kafka_destroy(consumer->rk);
    free(consumer);
    printf("[Kafka] Consumer 정리 완료\n");
}