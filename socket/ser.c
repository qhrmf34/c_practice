#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>

int client_sock;
int running = 1;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

// 시간 출력 함수
void print_time() {
    time_t now;
    struct tm *t;
    time(&now);
    t = localtime(&now);
    printf("[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
}

// 📩 받기 스레드
void* receive_thread(void* arg) {
    char buffer[1024];
    
    while(running) {
        memset(buffer, 0, sizeof(buffer));
        int recv_len = recv(client_sock, buffer, sizeof(buffer), 0);
        
        if (recv_len <= 0) {
            pthread_mutex_lock(&print_mutex);
            printf("\n");
            print_time();
            printf("❌ 클라이언트 연결 종료\n\n");
            pthread_mutex_unlock(&print_mutex);
            running = 0;
            break;
        }
        
        pthread_mutex_lock(&print_mutex);
        printf("\n");
        print_time();
        printf("클라이언트: %s\n\n", buffer);
        pthread_mutex_unlock(&print_mutex);
        
        if (strcmp(buffer, "quit") == 0) {
            running = 0;
            break;
        }
    }
    
    return NULL;
}

// 📤 보내기 스레드
void* send_thread(void* arg) {
    char message[1024];
    
    // 첫 입력 프롬프트
    pthread_mutex_lock(&print_mutex);
    print_time();
    printf("나: ");
    fflush(stdout);
    pthread_mutex_unlock(&print_mutex);
    
    while(running) {
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;
        
        if (strlen(message) == 0) {
            // 빈 입력이면 다시 프롬프트
            pthread_mutex_lock(&print_mutex);
            print_time();
            printf("나: ");
            fflush(stdout);
            pthread_mutex_unlock(&print_mutex);
            continue;
        }
        
        send(client_sock, message, strlen(message), 0);
        
        if (strcmp(message, "quit") == 0) {
            pthread_mutex_lock(&print_mutex);
            printf("\n🔚 채팅 종료\n");
            pthread_mutex_unlock(&print_mutex);
            running = 0;
            break;
        }
        
        // 다음 입력 프롬프트
        if (running) {
            pthread_mutex_lock(&print_mutex);
            print_time();
            printf("나: ");
            fflush(stdout);
            pthread_mutex_unlock(&print_mutex);
        }
    }
    
    return NULL;
}

int main() {
    // 소켓 생성
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    printf("✅ 서버 소켓 생성\n");
    
    // 소켓 재사용 옵션 (빠른 재시작용)
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // bind
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    printf("✅ 포트 8080 바인딩\n");
    
    // listen
    listen(server_sock, 5);
    printf("✅ 클라이언트 대기 중...\n\n");
    
    // accept
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_sock = accept(server_sock, 
                        (struct sockaddr*)&client_addr, 
                        &client_len);
    printf("🎉 클라이언트 연결됨!\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("💬 채팅 시작!\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    // 스레드 생성
    pthread_t recv_tid, send_tid;
    
    pthread_create(&recv_tid, NULL, receive_thread, NULL);
    pthread_create(&send_tid, NULL, send_thread, NULL);
    
    // 스레드 종료 대기
    pthread_join(recv_tid, NULL);
    pthread_join(send_tid, NULL);
    
    // 정리
    pthread_mutex_destroy(&print_mutex);
    close(client_sock);
    close(server_sock);
    printf("👋 서버 종료\n");
    
    return 0;
}