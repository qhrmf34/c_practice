#include "kafka_wrapper.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct 
KafkaProducer 
{
    rd_kafka_t *rk;
};

struct 
KafkaConsumer 
{
    rd_kafka_t *rk;
    rd_kafka_topic_partition_list_t *topics;
};

/* Producer */
KafkaProducer* 
kafka_producer_create(const char *brokers)
{
    char errstr[512];
    KafkaProducer *p = malloc(sizeof(*p));

    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr));

    p->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    printf("[Kafka] Producer ready\n");
    return p;
}

int 
kafka_producer_send(KafkaProducer *p,
                        const char *topic,
                        const char *key,
                        const char *data) 
{
    rd_kafka_producev(
        p->rk,
        RD_KAFKA_V_TOPIC(topic),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_VALUE((void*)data, strlen(data)),
        RD_KAFKA_V_KEY(key, key ? strlen(key) : 0),
        RD_KAFKA_V_END
    );
    rd_kafka_poll(p->rk, 0);
    return 0;
}

void 
kafka_producer_destroy(KafkaProducer *p) 
{
    rd_kafka_flush(p->rk, 5000);
    rd_kafka_destroy(p->rk);
    free(p);
}

/* Consumer */
KafkaConsumer* 
kafka_consumer_create(const char *brokers,
                      const char *group_id,
                      const char *topic) 
{
    char errstr[512];
    KafkaConsumer *c = malloc(sizeof(*c));

    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "group.id", group_id, errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "auto.offset.reset", "earliest", errstr, sizeof(errstr));

    c->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    rd_kafka_poll_set_consumer(c->rk);

    c->topics = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(c->topics, topic, -1);
    rd_kafka_subscribe(c->rk, c->topics);

    printf("[Kafka] Consumer ready\n");
    return c;
}

int kafka_consumer_receive(KafkaConsumer *c,
                           char *buffer,
                           size_t size,
                           int timeout_ms) 
{
    rd_kafka_message_t *msg = rd_kafka_consumer_poll(c->rk, timeout_ms);
    if (!msg) return 1;

    if (msg->err == 0) 
    {
        size_t len = msg->len < size-1 ? msg->len : size-1;
        memcpy(buffer, msg->payload, len);
        buffer[len] = '\0';
        rd_kafka_message_destroy(msg);
        return 0;
    }
    rd_kafka_message_destroy(msg);
    return -1;
}

void 
kafka_consumer_destroy(KafkaConsumer *c) 
{
    rd_kafka_consumer_close(c->rk);
    rd_kafka_destroy(c->rk);
    free(c);
}