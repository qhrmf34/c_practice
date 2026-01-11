#include <stdio.h>
#include <signal.h>
#include "kafka_wrapper.h"
#include "kafka_config.h"

static volatile sig_atomic_t running = 1;

void on_signal(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    KafkaConsumer *c =
        kafka_consumer_create(
            KAFKA_BROKERS,
            KAFKA_GROUP_ID,
            KAFKA_TOPIC);

    char buf[128];

    while (running) {
        int ret = kafka_consumer_poll(c, buf, sizeof(buf), 1000);
        if (ret == 0) {
            printf("[Consumer] %s\n", buf);
        }
    }

    kafka_consumer_destroy(c);
    return 0;
}
