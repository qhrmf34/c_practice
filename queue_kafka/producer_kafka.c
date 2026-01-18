#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "kafka_wrapper.h"
#include "kafka_config.h"

#define QUEUE_COUNT 10
#define MAX_CHECK   5

volatile sig_atomic_t running = 1;

void 
handler(int s) 
{ 
    running = 0; 
}

int 
main() 
{
    signal(SIGINT, handler);

    KafkaProducer *p = kafka_producer_create(KAFKA_BROKERS);

    int count[QUEUE_COUNT] = {0};
    int cur = 0;

    while (running) 
    {
        if (count[cur] < MAX_CHECK) 
        {
            // long 변수에 숫자 저장
            long num = cur * 111111111L;  // 0이면 0, 1이면 111111111, 2면 222222222
            
            char msg[16];
            sprintf(msg, "%09ld", num);  // %09ld = 9자리로, 부족하면 0으로 채움
            
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