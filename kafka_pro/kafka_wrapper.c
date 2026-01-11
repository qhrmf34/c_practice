#include "kafka_wrapper.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= Producer ================= */

struct KafkaProducer {
    rd_kafka_t *rk;
    rd_kafka_conf_t *conf;
    rd_kafka_topic_t *topic;
};

KafkaProducer* kafka_producer_create(const char *brokers,
                                     const char *topic_name) {
    char errstr[512];
    KafkaProducer *p = calloc(1, sizeof(*p));

    p->conf = rd_kafka_conf_new();
    rd_kafka_conf_set(p->conf, "bootstrap.servers",
                      brokers, errstr, sizeof(errstr));

    p->rk = rd_kafka_new(RD_KAFKA_PRODUCER,
                         p->conf, errstr, sizeof(errstr));
    if (!p->rk) {
        fprintf(stderr, "Producer 생성 실패: %s\n", errstr);
        free(p);
        return NULL;
    }

    p->topic = rd_kafka_topic_new(p->rk, topic_name, NULL);
    printf("[Kafka] Producer ready\n");
    return p;
}

int kafka_producer_send(KafkaProducer *p,
                         const char *key,
                         const char *data) {
    size_t key_len = key ? strlen(key) : 0;
    size_t data_len = strlen(data);

    if (rd_kafka_produce(
            p->topic,
            RD_KAFKA_PARTITION_UA,
            RD_KAFKA_MSG_F_COPY,
            (void*)data, data_len,
            (void*)key, key_len,
            NULL) == -1) {

        fprintf(stderr, "Produce 실패: %s\n",
                rd_kafka_err2str(rd_kafka_last_error()));
        return -1;
    }

    rd_kafka_poll(p->rk, 0);
    return 0;
}

void kafka_producer_destroy(KafkaProducer *p) {
    if (!p) return;

    rd_kafka_flush(p->rk, 5000);
    rd_kafka_topic_destroy(p->topic);
    rd_kafka_destroy(p->rk);
    free(p);
    printf("[Kafka] Producer 종료\n");
}

/* ================= Consumer ================= */

struct KafkaConsumer {
    rd_kafka_t *rk;
    rd_kafka_conf_t *conf;
    rd_kafka_topic_partition_list_t *topics;
};

KafkaConsumer* kafka_consumer_create(const char *brokers,
                                     const char *group_id,
                                     const char *topic) {
    char errstr[512];
    KafkaConsumer *c = calloc(1, sizeof(*c));

    c->conf = rd_kafka_conf_new();
    rd_kafka_conf_set(c->conf, "bootstrap.servers",
                      brokers, errstr, sizeof(errstr));
    rd_kafka_conf_set(c->conf, "group.id",
                      group_id, errstr, sizeof(errstr));
    rd_kafka_conf_set(c->conf, "auto.offset.reset",
                      "earliest", errstr, sizeof(errstr));

    c->rk = rd_kafka_new(RD_KAFKA_CONSUMER,
                         c->conf, errstr, sizeof(errstr));
    if (!c->rk) {
        fprintf(stderr, "Consumer 생성 실패: %s\n", errstr);
        free(c);
        return NULL;
    }

    rd_kafka_poll_set_consumer(c->rk);

    c->topics = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(
        c->topics, topic, RD_KAFKA_PARTITION_UA);

    rd_kafka_subscribe(c->rk, c->topics);
    printf("[Kafka] Consumer ready\n");
    return c;
}

int kafka_consumer_poll(KafkaConsumer *c,
                         char *buffer,
                         size_t size,
                         int timeout_ms) {
    rd_kafka_message_t *msg =
        rd_kafka_consumer_poll(c->rk, timeout_ms);

    if (!msg) return 1;

    if (msg->err) {
        rd_kafka_message_destroy(msg);
        return -1;
    }

    size_t len = msg->len < size - 1 ? msg->len : size - 1;
    memcpy(buffer, msg->payload, len);
    buffer[len] = '\0';

    rd_kafka_message_destroy(msg);
    return 0;
}

void kafka_consumer_destroy(KafkaConsumer *c) {
    if (!c) return;

    rd_kafka_consumer_close(c->rk);
    rd_kafka_topic_partition_list_destroy(c->topics);
    rd_kafka_destroy(c->rk);
    free(c);
    printf("[Kafka] Consumer 종료\n");
}
