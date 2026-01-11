// ═══════════════════════════════════════════════════════════
// queue.c - 큐 구현 (공유 메모리 관리 포함)
// ═══════════════════════════════════════════════════════════
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

// ═══════════════════════════════════════════════════════════
// 내부 구조체 (외부에 노출 안 됨)
// ═══════════════════════════════════════════════════════════
#define SHM_KEY 0x1234
#define DATA_SIZE 100

struct Queue {
    char data[MAX_QUEUE_SIZE][DATA_SIZE];  // 데이터 배열
    int front;                              // 앞쪽 인덱스
    int rear;                               // 뒤쪽 인덱스
    int count;                              // 데이터 개수
    int shmid;                              // 공유 메모리 ID
    pthread_mutex_t mutex;                  // 뮤텍스
    pthread_cond_t not_empty;               // 조건 변수
    pthread_cond_t not_full;                // 조건 변수
};

// ═══════════════════════════════════════════════════════════
// 큐 생성 (공유 메모리 생성 및 초기화)
// ═══════════════════════════════════════════════════════════
Queue* queue_create(void) {
    int shmid;
    Queue *q;
    
    // 공유 메모리 생성
    shmid = shmget(SHM_KEY, sizeof(Queue), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) {
        if (errno == EEXIST) {
            fprintf(stderr, "큐가 이미 존재합니다. queue_attach() 사용\n");
        } else {
            perror("shmget 실패");
        }
        return NULL;
    }
    
    // 공유 메모리 연결
    q = (Queue*)shmat(shmid, NULL, 0);
    if (q == (Queue*)-1) {
        perror("shmat 실패");
        shmctl(shmid, IPC_RMID, NULL);  // 실패 시 삭제
        return NULL;
    }
    
    // 큐 초기화
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    q->shmid = shmid;
    
    // 프로세스 간 공유 가능한 뮤텍스 설정
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&q->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    // 프로세스 간 공유 가능한 조건 변수 설정
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&q->not_empty, &cond_attr);
    pthread_cond_init(&q->not_full, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
    
    printf("[Queue] 큐 생성 완료 (shmid: %d)\n", shmid);
    return q;
}

// ═══════════════════════════════════════════════════════════
// 기존 큐에 연결
// ═══════════════════════════════════════════════════════════
Queue* queue_attach(void) {
    int shmid;
    Queue *q;
    
    // 기존 공유 메모리 가져오기
    shmid = shmget(SHM_KEY, sizeof(Queue), 0666);
    if (shmid < 0) {
        perror("shmget 실패 (큐가 생성되지 않았습니다)");
        return NULL;
    }
    
    // 공유 메모리 연결
    q = (Queue*)shmat(shmid, NULL, 0);
    if (q == (Queue*)-1) {
        perror("shmat 실패");
        return NULL;
    }
    
    printf("[Queue] 큐 연결 완료 (shmid: %d)\n", shmid);
    return q;
}

// ═══════════════════════════════════════════════════════════
// 큐 연결 해제
// ═══════════════════════════════════════════════════════════
void queue_detach(Queue *q) {
    if (q == NULL) return;
    
    if (shmdt(q) < 0) {
        perror("shmdt 실패");
    } else {
        printf("[Queue] 큐 연결 해제\n");
    }
}

// ═══════════════════════════════════════════════════════════
// 큐 삭제 (공유 메모리 삭제)
// ═══════════════════════════════════════════════════════════
void queue_destroy(Queue *q) {
    if (q == NULL) return;
    
    int shmid = q->shmid;
    
    // 뮤텍스 및 조건 변수 삭제
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    
    // 연결 해제
    queue_detach(q);
    
    // 공유 메모리 삭제
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        perror("shmctl 실패");
    } else {
        printf("[Queue] 큐 삭제 완료\n");
    }
}

// ═══════════════════════════════════════════════════════════
// enqueue: 데이터 삽입
// ═══════════════════════════════════════════════════════════
int queue_enqueue(Queue *q, const char *data) {
    if (q == NULL || data == NULL) return -1;
    
    pthread_mutex_lock(&q->mutex);
    
    // 큐가 꽉 찰 때까지 대기
    while (q->count >= MAX_QUEUE_SIZE) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    
    // 데이터 복사
    strncpy(q->data[q->rear], data, DATA_SIZE - 1);
    q->data[q->rear][DATA_SIZE - 1] = '\0';  // null 종료 보장
    
    // 인덱스 업데이트
    q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    q->count++;
    
    // 신호 전송
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    
    return 0;
}

// ═══════════════════════════════════════════════════════════
// dequeue: 데이터 추출
// ═══════════════════════════════════════════════════════════
int queue_dequeue(Queue *q, char *buffer, size_t size) {
    if (q == NULL || buffer == NULL) return -1;
    
    pthread_mutex_lock(&q->mutex);
    
    // 큐가 빌 때까지 대기
    while (q->count <= 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    
    // 데이터 복사
    strncpy(buffer, q->data[q->front], size - 1);
    buffer[size - 1] = '\0';  // null 종료 보장
    
    // 인덱스 업데이트
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->count--;
    
    // 신호 전송
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    
    return 0;
}

// ═══════════════════════════════════════════════════════════
// 유틸리티 함수들
// ═══════════════════════════════════════════════════════════
int queue_count(Queue *q) {
    if (q == NULL) return -1;
    
    pthread_mutex_lock(&q->mutex);
    int count = q->count;
    pthread_mutex_unlock(&q->mutex);
    
    return count;
}

int queue_is_empty(Queue *q) {
    return queue_count(q) == 0;
}

int queue_is_full(Queue *q) {
    return queue_count(q) >= MAX_QUEUE_SIZE;
}