#ifndef KAFKA_WRAPPER_H
#define KAFKA_WRAPPER_H

#include <stddef.h>

typedef struct KafkaProducer KafkaProducer;
typedef struct KafkaConsumer KafkaConsumer;

KafkaProducer* 
kafka_producer_create(const char *brokers);

int 
kafka_producer_send(KafkaProducer *producer,
                        const char *topic,
                        const char *key,
                        const char *data);
void 
kafka_producer_destroy(KafkaProducer *producer);

KafkaConsumer* 
kafka_consumer_create(const char *brokers,
                                     const char *group_id,
                                     const char *topic);
int 
kafka_consumer_receive(KafkaConsumer *consumer,
                           char *buffer,
                           size_t size,
                           int timeout_ms);
void 
kafka_consumer_destroy(KafkaConsumer *consumer);

#endif