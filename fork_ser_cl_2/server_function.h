#ifndef SERVER_FUNCTION_H
#define SERVER_FUNCTION_H

#include <arpa/inet.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define PORT 9190
#define MAX_SESSIONS 32

typedef struct 
{
    int sock;
    struct sockaddr_in addr;
    int session_id;
} ClientInfo;

// 전역 변수 선언 (extern)
extern int session_count;
extern pthread_mutex_t mutex;

// 클라이언트 처리 스레드 함수
void 
*handle_client_thread(void *arg);

// 서버 소켓 생성 및 설정
int 
create_server_socket(void);

// 채팅방 프로세스 실행
void 
un_chat_room(int serv_sock);

#endif

