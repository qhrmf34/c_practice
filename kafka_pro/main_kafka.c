#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static volatile sig_atomic_t running = 1;

void on_signal(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    pid_t prod = fork();
    if (prod == 0)
        execl("./producer_kafka", "producer_kafka", NULL);

    pid_t cons = fork();
    if (cons == 0)
        execl("./consumer_kafka", "consumer_kafka", NULL);

    while (running) sleep(1);

    kill(prod, SIGTERM);
    kill(cons, SIGTERM);

    waitpid(prod, NULL, 0);
    waitpid(cons, NULL, 0);

    printf("[Main] 종료 완료\n");
    return 0;
}
