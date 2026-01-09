#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 10

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int loop_count = 0; // 전체 진행 상황

void* thread_func(void* arg) {
    int id = *(int*)arg;

    pthread_mutex_lock(&lock);
    sleep(1);
    loop_count++; // 현재 스레드 순서로 count 증가
    printf("[Thread %d] PID: %d, Loop: %d/%d\n", id, getpid(), loop_count, NUM_THREADS);
    pthread_mutex_unlock(&lock);


    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    int ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i + 1;
        pthread_create(&threads[i], NULL, thread_func, &ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    printf("Loop End!\n");

    return 0;
}
