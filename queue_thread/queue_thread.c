#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// 큐 구조체 정의
#define MAX 10  // 큐의 최대 크기

typedef struct {
    long data[MAX];             // ✨ long 배열로 변경 (숫자 저장)
    int front;                  // 앞쪽 인덱스 (꺼낼 위치)
    int rear;                   // 뒤쪽 인덱스 (넣을 위치)
    int count;                  // 현재 큐에 있는 데이터 개수
    pthread_mutex_t mutex;      // 동기화 잠금
    pthread_cond_t not_empty;   // "큐 안 비었어!" 조건 변수
    pthread_cond_t not_full;    // "큐 안 찼어!" 조건 변수
} Queue;

// 큐 초기화
void queue_init(Queue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

// 큐가 비었는지 확인
int valid_check(Queue *q) {
    return q->count == 0;
}

// 큐가 꽉 찼는지 확인
int max_Check(Queue *q) {
    return q->count == MAX;
}

// enqueue: 큐에 long 데이터 넣기
void enqueue(Queue *q, long item) {  // ✨ long 타입으로 변경
    pthread_mutex_lock(&q->mutex);
    
    while(max_Check(q)) {
        printf("생산자: 큐 꽉 참! 대기 중... (count=%d)\n", q->count);
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    
    // ✨ long 값을 직접 저장
    q->data[q->rear] = item;
    printf("생산자: [%d] 위치에 %ld 삽입\n", q->rear, item);
    
    q->rear = (q->rear + 1) % MAX;
    q->count++;
    printf("생산자: 현재 큐에 %d개 데이터\n\n", q->count);
    
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

// dequeue: 큐에서 long 데이터 꺼내기
long dequeue(Queue *q) {  // ✨ long 반환으로 변경
    pthread_mutex_lock(&q->mutex);
    
    while(valid_check(q)) {
        printf("소비자: 큐 비었음! 대기 중... (count=%d)\n", q->count);
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    
    // ✨ long 값을 직접 꺼내기
    long item = q->data[q->front];
    printf("소비자: [%d] 위치에서 %ld 추출\n", q->front, item);
    
    q->front = (q->front + 1) % MAX;
    q->count--;
    printf("소비자: 현재 큐에 %d개 데이터\n\n", q->count);
    
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    
    return item;  // ✨ long 값 반환
}

// 생산자 스레드
void* producer(void *arg) {
    Queue *q = (Queue*)arg;
    int i = 0;
    
    while(1) {
        // ✨ long 변수에 반복 숫자 생성
        long num = (i % 10) * 111111111L;  
        // 0→0, 1→111111111, 2→222222222, ..., 9→999999999, 10→0, ...
        
        enqueue(q, num);  // ✨ long 값 삽입
        
        i++;
        usleep(1000000);  // 1초 대기
    }
    
    return NULL;
}

// 소비자 스레드
void* consumer(void *arg) {
    Queue *q = (Queue*)arg;
    
    while(1) {
        // ✨ long 값으로 받기
        long num = dequeue(q);
        
        // 처리 (9자리 문자열로 출력)
        printf("소비자: %09ld 처리 완료!\n\n", num);
        
        usleep(3000000);  // 3초 대기
    }
    
    return NULL;
}

// 메인 함수
int main() {
    Queue queue;
    pthread_t producer_tid, consumer_tid;
    
    queue_init(&queue);
    
    printf("═══════════════════════════════════\n");
    printf("   큐 기반 생산자-소비자 시작!\n");
    printf("   큐 크기: %d\n", MAX);
    printf("   데이터 타입: long (9자리 반복)\n");
    printf("═══════════════════════════════════\n\n");
    
    pthread_create(&producer_tid, NULL, producer, &queue);
    pthread_create(&consumer_tid, NULL, consumer, &queue);
    
    pthread_join(producer_tid, NULL);
    pthread_join(consumer_tid, NULL);
    
    return 0;
}