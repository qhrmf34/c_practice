#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

int session_count = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// 클라이언트 처리 스레드
void *handle_client_thread(void *arg) {
    ClientInfo *member = (ClientInfo*)arg;
    char buf[BUF_SIZE];
    int str_len;
    int count = 0;

    printf("  [Thread #%d] Member connected (fd: %d)\n",
           member->session_id, member->sock);

    while (count < 10) {
        str_len = read(member->sock, buf, BUF_SIZE - 1);
        if (str_len <= 0) {
            if (str_len == 0)
                printf("  [Thread #%d] Client closed connection\n", member->session_id);
            else
                perror("read() error");
            break;
        }

        buf[str_len] = 0;
        printf("  [Thread #%d] Received: %s", member->session_id, buf);

        // 메시지 그대로 Echo
        write(member->sock, buf, str_len);

        count++;
        sleep(1);
    }

    // 세션 종료 처리
    pthread_mutex_lock(&mutex);
    session_count--;
    printf("  [Thread #%d] Member left\n", member->session_id);
    printf("[C.P] Current members: %d/%d\n\n", session_count, MAX_SESSIONS);
    pthread_mutex_unlock(&mutex);

    close(member->sock);
    free(member);
    return NULL;
}

// 서버 소켓 생성
int create_server_socket(void) {
    int serv_sock;
    struct sockaddr_in serv_addr;
    int option = 1;

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) {
        perror("socket() error");
        return -1;
    }

    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind() error");
        close(serv_sock);
        return -1;
    }

    if (listen(serv_sock, 32) == -1) {
        perror("listen() error");
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

    printf("\n[C.P %d] Chat Room Started\n", getpid());
    printf("[C.P] Max capacity: %d members\n\n", MAX_SESSIONS);

    while (1) {
        pthread_mutex_lock(&mutex);
        
        if (session_count >= MAX_SESSIONS) {
            pthread_mutex_unlock(&mutex);
            
            sleep(1);
            continue;  // accept 안 하고 다시 체크
        }
        
        int sid = session_count + 1;
        session_count++;
        pthread_mutex_unlock(&mutex);
        
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
        
        if (clnt_sock == -1) {
            // accept 실패하면 카운트 되돌리기
            pthread_mutex_lock(&mutex);
            session_count--;
            pthread_mutex_unlock(&mutex);
            continue;
        }

        printf("[C.P] New member joined: %s:%d (Session #%d)\n",
               inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), sid);

        // 클라이언트 구조체 생성
        ClientInfo *member = malloc(sizeof(ClientInfo));
        member->sock = clnt_sock;
        member->addr = clnt_addr;
        member->session_id = sid;

        // 스레드 생성
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client_thread, (void*)member) != 0) {
            perror("pthread_create() error");
            
            // 스레드 생성 실패하면 정리
            close(clnt_sock);
            free(member);
            
            pthread_mutex_lock(&mutex);
            session_count--;
            pthread_mutex_unlock(&mutex);
            continue;
        }
        pthread_detach(thread);

        printf("[C.P] Member #%d thread started\n", sid);
        printf("[C.P] Current members: %d/%d\n\n", session_count, MAX_SESSIONS);
    }
}
