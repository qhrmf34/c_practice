#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

int session_count = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client_thread(void *arg) {
    ClientInfo *member = (ClientInfo*)arg;
    char buf[BUF_SIZE];
    int str_len;
    
    printf("  [Thread #%d] Member connected (fd: %d)\n", 
           member->session_id, member->sock);
    
    while(1) {
        str_len = read(member->sock, buf, BUF_SIZE);
        if (str_len <= 0) {
            break;
        }
        
        buf[str_len] = 0;
        printf("  [Thread #%d] Received: %s", member->session_id, buf);
        
        sleep(1);
        write(member->sock, buf, str_len);  // Echo
    }
    
    pthread_mutex_lock(&mutex);
    session_count--;
    printf("  [Thread #%d] Member left\n", member->session_id);
    printf("[C.P] Current members: %d/%d\n\n", session_count, MAX_SESSIONS);
    pthread_mutex_unlock(&mutex);
    
    close(member->sock);
    free(member);
    return NULL;
}

int create_server_socket(void) {
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;
    
    // Socket 생성
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) {
        printf("socket() error\n");
        return -1;
    }
    
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    
    // Bind
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);
    
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        printf("bind() error\n");
        close(serv_sock);
        return -1;
    }
    
    // Listen
    if (listen(serv_sock, 5) == -1) {
        printf("listen() error\n");
        close(serv_sock);
        return -1;
    }
    
    printf("[M.P] Server ready on port %d\n", PORT);
    return serv_sock;
}

void run_chat_room(int serv_sock) {
    int clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    
    printf("\n[C.P %d] Chat Room Started (sess 0)\n", getpid());
    printf("[C.P] Max capacity: %d members\n\n", MAX_SESSIONS);
    
    // C.P가 accept 루프 (최대 32개)
    while(1) {
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
        
        if (clnt_sock == -1) {
            continue;
        }
        
        pthread_mutex_lock(&mutex);
        
        // 32개 제한
        if (session_count >= MAX_SESSIONS) {
            printf("[C.P] Chat room FULL! (%d/%d).\n", 
                   session_count, MAX_SESSIONS);
            pthread_mutex_unlock(&mutex);
            
            char *msg = "Chat room is full (32/32). T.\n";
            write(clnt_sock, msg, strlen(msg));
            close(clnt_sock);
            continue;
        }
        
        session_count++;
        int sid = session_count;
        pthread_mutex_unlock(&mutex);
        
        printf("[C.P] New member joined: %s:%d\n", 
               inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
        // 새 스레드 생성 (각 멤버)
        ClientInfo *member = (ClientInfo*)malloc(sizeof(ClientInfo));
        member->sock = clnt_sock;
        member->addr = clnt_addr;
        member->session_id = sid;
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client_thread, (void*)member);
        pthread_detach(thread);
        
        printf("[C.P] Member #%d thread started\n", sid);
        printf("[C.P] Current members: %d/%d\n\n", session_count, MAX_SESSIONS);
    }
}

