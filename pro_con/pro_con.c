#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// 큐 구조체 정의
#define MAX 10  // 큐의 최대 크기

typedef struct {
    char data[MAX][100];        // 데이터를 저장할 배열 (10개, 각 100바이트)
    int front;                  // 앞쪽 인덱스 (꺼낼 위치)
    int rear;                   // 뒤쪽 인덱스 (넣을 위치)
    int count;                  // 현재 큐에 있는 데이터 개수
    pthread_mutex_t mutex;      // 동기화 잠금
    pthread_cond_t not_empty;   // "큐 안 비었어!" 조건 변수
    pthread_cond_t not_full;    // "큐 안 찼어!" 조건 변수
} Queue;

// 큐 초기화
void queue_init(Queue *q) {
    q->front = 0;                                    // 앞쪽 인덱스 0으로
    q->rear = 0;                                     // 뒤쪽 인덱스 0으로
    q->count = 0;                                    // 데이터 개수 0
    pthread_mutex_init(&q->mutex, NULL);             // 뮤텍스 초기화
    pthread_cond_init(&q->not_empty, NULL);          // 조건변수 초기화
    pthread_cond_init(&q->not_full, NULL);           // 조건변수 초기화
}

// 큐가 비었는지 확인
int valid_check(Queue *q) {
    return q->count == 0;  // count가 0이면 비어있음
}

// 큐가 꽉 찼는지 확인
int max_check(Queue *q) {
    return q->count == MAX;  // count가 MAX면 꽉 참
}

// enqueue: 큐에 데이터 넣기 (생산자가 사용)
void enqueue(Queue *q, const char *item) {
    pthread_mutex_lock(&q->mutex);  // 잠금
    
    // MAX 체크: 큐가 꽉 찼는지 확인
    while(max_check(q)) {
        printf("생산자: 큐 꽉 참! 대기 중... (count=%d)\n", q->count);
        pthread_cond_wait(&q->not_full, &q->mutex);  // 🚦 공간 생길 때까지 대기
    }
    
    // 데이터를 rear 위치에 저장
    strcpy(q->data[q->rear], item);  // 문자열 복사
    printf("생산자: [%d] 위치에 '%s' 삽입\n", q->rear, item);
    
    // rear 인덱스 업데이트 (순환)
    q->rear = (q->rear + 1) % MAX;  // 0→1→2→...→9→0→1...
    
    // count 증가 (valid data count)
    q->count++;
    printf("생산자: 현재 큐에 %d개 데이터\n\n", q->count);
    
    pthread_cond_signal(&q->not_empty);  // 데이터 있다는 신호
    pthread_mutex_unlock(&q->mutex);     // 잠금 해제
}

// dequeue: 큐에서 데이터 꺼내기 (소비자가 사용)
void dequeue(Queue *q, char *item) {
    pthread_mutex_lock(&q->mutex);  // 잠금
    
    // Valid 체크: 큐가 비었는지 확인
    while(valid_check(q)) {
        printf("소비자: 큐 비었음! 대기 중... (count=%d)\n", q->count);
        pthread_cond_wait(&q->not_empty, &q->mutex);  // 🚦 데이터 생길 때까지 대기
    }
    
    // front 위치에서 데이터 꺼내기
    strcpy(item, q->data[q->front]);  // 문자열 복사
    printf("소비자: [%d] 위치에서 '%s' 추출\n", q->front, item);
    
    // front 인덱스 업데이트 (순환)
    q->front = (q->front + 1) % MAX;  // 0→1→2→...→9→0→1...
    
    // count 감소
    q->count--;
    printf("소비자: 현재 큐에 %d개 데이터\n\n", q->count);
    
    pthread_cond_signal(&q->not_full);  // 공간 있다는 신호
    pthread_mutex_unlock(&q->mutex);    // 잠금 해제
}

// 생산자 스레드
void* producer(void *arg) {
    Queue *q = (Queue*)arg;  // 큐 포인터 받기
    char buffer[100];        // 데이터 만들 임시 버퍼
    int i = 0;               // 데이터 번호
    
    while(1) {
        // sprintf로 데이터 생성
        sprintf(buffer, "Data-%d", i);  
        // sprintf = 문자열을 포맷팅해서 buffer에 저장
        // 예: "Data-0", "Data-1", "Data-2", ...
        
        // 큐에 데이터 삽입
        enqueue(q, buffer);
        
        i++;
        usleep(100000);  // 0.1초 대기 (생산 속도 조절)
    }
    
    return NULL;
}

// 소비자 스레드
void* consumer(void *arg) {
    Queue *q = (Queue*)arg;  // 큐 포인터 받기
    char buffer[100];        // 데이터 받을 버퍼
    
    while(1) {
        // 큐에서 데이터 꺼내기
        dequeue(q, buffer);
        
        // 데이터 처리 (여기서는 그냥 출력)
        printf("소비자: '%s' 처리 완료!\n\n", buffer);
        
        usleep(300000);  // 0.3초 대기 (소비 속도 조절)
    }
    
    return NULL;
}

// 메인 함수
int main() {
    Queue queue;  // 큐 선언
    pthread_t producer_tid, consumer_tid;
    
    // 큐 초기화
    queue_init(&queue);
    
    printf("   큐 기반 생산자-소비자 시작!\n");
    printf("   큐 크기: %d\n", MAX);
    printf("═══════════════════════════════════\n\n");
    
    // 스레드 생성
    pthread_create(&producer_tid, NULL, producer, &queue);
    pthread_create(&consumer_tid, NULL, consumer, &queue);
    
    // 스레드 종료 대기
    pthread_join(producer_tid, NULL);
    pthread_join(consumer_tid, NULL);
    
    return 0;
}
