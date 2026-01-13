#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    int serv_sock;
    pid_t pid;
    
    printf("Max Sessions: %d\n\n", MAX_SESSIONS);
    
    // 서버 소켓 생성 및 설정
    serv_sock = create_server_socket();
    if (serv_sock == -1) {
        return -1;
    }
    
    
    // Fork - 채팅방 생성
    pid = fork();
    
    if (pid == -1) {
        printf("fork() error\n");
        close(serv_sock);
        return -1;
    }
    
    if (pid == 0) {
        printf("[M.P] Creating Child Process (%d)...\n",getpid());
        //  Child Process
        run_chat_room(serv_sock);
        exit(0);
    }
    else {
        //  Parent Process 
        close(serv_sock);  // M.P는 서버 소켓 닫음 (C.P만 사용)
        printf("[M.P] Waiting for chat room to close...\n\n");
        
        // M.P는 C.P가 끝날 때까지 대기
        int status;
        waitpid(pid, &status, 0);
        
        printf("[M.P] Chat room closed\n");
    }
    
    return 0;
}