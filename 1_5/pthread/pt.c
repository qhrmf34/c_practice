#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 10
#define LOOP_COUNT 5

// 스레드 함수
void* 
thread_function(void* arg) 
{
    int thread_id = *(int*)arg;
    
    for (int i = 1; i <= LOOP_COUNT; i++) 
    {
        printf("[Thread %2d] PID: %d, Loop: %d/%d\n", 
               thread_id, getpid(), i, LOOP_COUNT);
        sleep(1);
    }
    
    printf("[Thread %2d] Loop End!\n", thread_id);
    
    return NULL;
}

int main() 
{
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    printf("=== 10개 스레드 생성 시작 ===\n");
    printf("메인 PID: %d\n\n", getpid());
    
    // 10개 스레드 생성
    for (int i = 0; i < NUM_THREADS; i++) 
    {
        thread_ids[i] = i + 1;  // 1부터 10까지
        pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]);
        printf("thread %d 생성 완료\n", i + 1);
    }
    
    printf("\n=== 모든 스레드 실행 중 ===\n\n");
    
    // 모든 스레드 종료 대기
    for (int i = 0; i < NUM_THREADS; i++) 
    {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n=== 모든 스레드 종료 완료 ===\n");
    printf("메인 스레드 종료\n");
    
    return 0;
}