#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "kafka_wrapper.h"
#include "kafka_config.h"

volatile sig_atomic_t running = 1;

void handler(int s) { running = 0; }

int validcheck(const char *msg) {
    if (strlen(msg) != 9) return 0;
    char c = msg[0];
    if (c < '0' || c > '9') return 0;
    for (int i = 1; i < 9; i++)
        if (msg[i] != c) return 0;
    return 1;
}

int main() {
    signal(SIGINT, handler);

    KafkaConsumer *c =
        kafka_consumer_create(KAFKA_BROKERS, KAFKA_GROUP_ID, KAFKA_TOPIC);

    char buf[32];
    while (running) {
        if (kafka_consumer_receive(c, buf, sizeof(buf), 1000) == 0) {
            if (validcheck(buf))
                printf("[Consumer] VALID   %s\n", buf);
            else
                printf("[Consumer] INVALID %s\n", buf);
        }
    }
    kafka_consumer_destroy(c);
    return 0;
}