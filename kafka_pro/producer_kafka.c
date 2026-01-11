#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "kafka_wrapper.h"
#include "kafka_config.h"

#define QUEUE_COUNT 10
#define MAX_CHECK   5

volatile sig_atomic_t running = 1;

void handler(int s) { running = 0; }

int main() {
    signal(SIGINT, handler);

    KafkaProducer *p = kafka_producer_create(KAFKA_BROKERS);

    int count[QUEUE_COUNT] = {0};
    int cur = 0;

    while (running) {
        if (count[cur] < MAX_CHECK) {
            char msg[16];
            sprintf(msg, "%d%d%d%d%d%d%d%d%d",
                    cur,cur,cur,cur,cur,cur,cur,cur,cur);

            kafka_producer_send(p, KAFKA_TOPIC, NULL, msg);
            printf("[Producer] %s\n", msg);
            count[cur]++;
        }
        cur = (cur + 1) % QUEUE_COUNT;
        usleep(500000);
    }

    kafka_producer_destroy(p);
    return 0;
}
