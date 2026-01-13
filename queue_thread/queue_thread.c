#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <inttypes.h>

#define MAX 10
#define RUN_SECONDS 600

typedef struct {
    long data[MAX];
    int front;
    int rear;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} Queue;

typedef struct {
    uint64_t produced_count;
    uint64_t consumed_count;
    uint64_t error_count;
    long expected_next;
    pthread_mutex_t mutex;
} Statistics;

volatile sig_atomic_t running = 1;
Statistics stats;
Queue queue;

void signal_handler(int sig) {
    running = 0;
    pthread_mutex_lock(&queue.mutex);
    pthread_cond_broadcast(&queue.not_empty);
    pthread_cond_broadcast(&queue.not_full);
    pthread_mutex_unlock(&queue.mutex);
}


void queue_init(Queue *q) {
    q->front = q->rear = q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void stats_init(Statistics *s) {
    s->produced_count = 0;
    s->consumed_count = 0;
    s->error_count = 0;
    s->expected_next = 0;
    pthread_mutex_init(&s->mutex, NULL);
}

int valid_check(Queue *q) { return q->count == 0; }
int max_Check(Queue *q) { return q->count == MAX; }

int extract_digit(long num) {
    if (num == 0) return 0;
    return (int)(num / 111111111L);
}

int validate_sequence(long num, long expected) {
    return extract_digit(num) == expected;
}

void enqueue(Queue *q, long item) {
    pthread_mutex_lock(&q->mutex); 
    while (max_Check(q) && running) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 10000000;  // 1ms
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&q->not_full, &q->mutex, &ts);
    }
    if (!running) { pthread_mutex_unlock(&q->mutex); return; }
    q->data[q->rear] = item;
    printf("생산자: [%d] 위치에 %ld 삽입\n", q->rear, item);
    q->rear = (q->rear + 1) % MAX;
    q->count++;
    printf("생산자: 현재 큐에 %d개 데이터\n\n", q->count);

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int dequeue(Queue *q, long *item) {
    pthread_mutex_lock(&q->mutex);
    while (valid_check(q) && running) {
        printf("소비자: 큐 비었음! 대기 중... (count=%d)\n", q->count);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 1000000;  // 1ms
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&q->not_empty, &q->mutex, &ts);
    }
    if (!running && valid_check(q)) { pthread_mutex_unlock(&q->mutex); return 0; }
    *item = q->data[q->front];
    printf("소비자: [%d] 위치에서 %ld 추출\n", q->front, *item);
    q->front = (q->front + 1) % MAX;
    q->count--;
    printf("소비자: 현재 큐에 %d개 데이터\n\n", q->count);
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return 1;
} 

void* producer(void* arg) {
    long i = 0;
    while (running) {
        long num = (i % 10) * 111111111L;
        enqueue(&queue, num);
        pthread_mutex_lock(&stats.mutex);
        stats.produced_count++;
        pthread_mutex_unlock(&stats.mutex);
        i++;
    }
    printf("\n[생산자] 종료 - 총 %" PRIu64 "개 생산\n", stats.produced_count);
    return NULL;
}

void* consumer(void* arg) {
    long num;
    while (running) {
        if (dequeue(&queue, &num)) {
            pthread_mutex_lock(&stats.mutex);
            if (!validate_sequence(num, stats.expected_next)) {
                int digit = extract_digit(num);
                stats.error_count++;
                printf("\n[오류 #% " PRIu64 "] 기대:%ld 실제:%d\n", stats.error_count, stats.expected_next, digit);
                stats.expected_next = (digit + 1) % 10;
            } else {
                stats.expected_next = (stats.expected_next + 1) % 10;
            }
            stats.consumed_count++;
            pthread_mutex_unlock(&stats.mutex);
        }
    }
    printf("[소비자] 종료 - 총 %" PRIu64 "개 소비, 오류 %" PRIu64 "개\n", stats.consumed_count, stats.error_count);
    return NULL;
}



// 타이머
void* timer_thread(void* arg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += RUN_SECONDS;  // 정확히 RUN_SECONDS 후 종료
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

    running = 0;
    pthread_mutex_lock(&queue.mutex);
    pthread_cond_broadcast(&queue.not_empty);
    pthread_cond_broadcast(&queue.not_full);
    pthread_mutex_unlock(&queue.mutex);

    printf("\n%ds 경과! 프로그램 종료 중...\n", RUN_SECONDS);
    return NULL;
}

int main() {
    pthread_t prod, cons, stats_tid, timer_tid;
    signal(SIGINT, signal_handler);

    queue_init(&queue);
    stats_init(&stats);

    time_t start_time = time(NULL);

    pthread_create(&timer_tid, NULL, timer_thread, NULL);
    pthread_create(&prod, NULL, producer, NULL);
    pthread_create(&cons, NULL, consumer, NULL);

    pthread_join(timer_tid, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    uint64_t total_time = time(NULL) - start_time;

    printf("\n최종 통계\n");
    printf("총 실행 시간: %" PRIu64 "초\n", total_time);
    printf("총 생산 개수: %" PRIu64 "개\n", stats.produced_count);
    printf("총 소비 개수: %" PRIu64 "개\n", stats.consumed_count);
    printf("순서 오류: %" PRIu64 "개\n", stats.error_count);

    return 0;
}
