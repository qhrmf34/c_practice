#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    int serv_sock;
    pid_t pid;

    printf("Max Sessions: %d\n\n", MAX_SESSIONS);

    serv_sock = create_server_socket();
    if (serv_sock == -1)
        return -1;

    pid = fork();
    if (pid == -1) {
        perror("fork() error");
        close(serv_sock);
        return -1;
    }

    if (pid == 0) {
        // Child Process - Chat Room
        printf("[M.P] Creating Child Process (%d)...\n", getpid());
        run_chat_room(serv_sock);
        exit(0);
    } else {
        // Main Process
        close(serv_sock);
        printf("[M.P] Waiting for chat room to close...\n\n");
        int status;
        waitpid(pid, &status, 0);
        printf("[M.P] Chat room closed\n");
    }

    return 0;
}