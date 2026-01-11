#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    if (fork() == 0)
        execl("./producer_kafka", "producer_kafka", NULL);

    if (fork() == 0)
        execl("./consumer_kafka", "consumer_kafka", NULL);

    while (1) pause();
}
