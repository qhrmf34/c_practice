// ═══════════════════════════════════════════════════════════
// queue.h - 큐 인터페이스 (헤더)
// ═══════════════════════════════════════════════════════════
#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>

#define MAX_QUEUE_SIZE 10

// ═══════════════════════════════════════════════════════════
// 불투명 포인터 (Opaque Pointer)
// 내부 구조를 숨기고 포인터만 노출
// ═══════════════════════════════════════════════════════════
typedef struct Queue Queue;

// ═══════════════════════════════════════════════════════════
// 공개 API 함수들
// ═══════════════════════════════════════════════════════════

/**
 * 공유 메모리에 큐 생성
 * @return 큐 포인터, 실패 시 NULL
 */
Queue* queue_create(void);

/**
 * 기존 공유 메모리의 큐에 연결
 * @return 큐 포인터, 실패 시 NULL
 */
Queue* queue_attach(void);

/**
 * 큐에서 연결 해제
 * @param q 큐 포인터
 */
void queue_detach(Queue *q);

/**
 * 큐 삭제 (공유 메모리 해제)
 * @param q 큐 포인터
 */
void queue_destroy(Queue *q);

/**
 * 큐에 데이터 삽입 (블로킹)
 * @param q 큐 포인터
 * @param data 삽입할 데이터
 * @return 성공 0, 실패 -1
 */
int queue_enqueue(Queue *q, const char *data);

/**
 * 큐에서 데이터 추출 (블로킹)
 * @param q 큐 포인터
 * @param buffer 데이터를 저장할 버퍼
 * @param size 버퍼 크기
 * @return 성공 0, 실패 -1
 */
int queue_dequeue(Queue *q, char *buffer, size_t size);

/**
 * 큐의 현재 데이터 개수
 * @param q 큐 포인터
 * @return 데이터 개수
 */
int queue_count(Queue *q);

/**
 * 큐가 비었는지 확인
 * @param q 큐 포인터
 * @return 비었으면 1, 아니면 0
 */
int queue_is_empty(Queue *q);

/**
 * 큐가 꽉 찼는지 확인
 * @param q 큐 포인터
 * @return 꽉 찼으면 1, 아니면 0
 */
int queue_is_full(Queue *q);

#endif // QUEUE_H