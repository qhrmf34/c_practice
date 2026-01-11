#include <stdio.h>
#include <unistd.h>
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

    KafkaProducer *p =
        kafka_producer_create(KAFKA_BROKERS, KAFKA_TOPIC);

    int i = 0;
    char msg[128];

    while (running) {
        snprintf(msg, sizeof(msg),
                 "Message-%d (PID:%d)", i++, getpid());
        kafka_producer_send(p, NULL, msg);
        printf("[Producer] %s\n", msg);
        usleep(500000);
    }

    kafka_producer_destroy(p);
    return 0;
}
