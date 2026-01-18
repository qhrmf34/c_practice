#include <stdio.h>        // printf, fgets 등 입출력 함수 사용
#include <string.h>       // strlen, strcmp, memset 등 문자열 함수
#include <unistd.h>       // close 함수 (소켓 닫기)
#include <pthread.h>      // 스레드 관련 함수들
#include <arpa/inet.h>    // 네트워크 통신 함수들 (socket, bind, connect 등)
#include <time.h>         // 시간 관련 함수

// 전역 변수 (모든 함수에서 접근 가능)
int client_sock;          // 클라이언트와 통신할 소켓 번호
int running = 1;          // 프로그램 실행 상태 (1=실행중, 0=종료)
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER; // 출력 잠금장치 초기화

// 시간 출력 함수
void 
print_time() 
{
    time_t now;                    // 현재 시간을 저장할 변수
    struct tm *t;                  // 시간 구조체 포인터
    time(&now);                    // 현재 시간 가져오기
    t = localtime(&now);           // 우리나라 시간으로 변환
    printf("[%02d:%02d:%02d] ",    // [시:분:초] 형식으로 출력
           t->tm_hour,             // 시간 (0~23)
           t->tm_min,              // 분 (0~59)
           t->tm_sec);             // 초 (0~59)
}

// 메시지 받기 스레드 (계속 상대방 메시지 기다림)
void* 
receive_thread(void* arg) 
{  // 스레드 함수 (void* 매개변수 필수)
    char buffer[1024];             // 받은 메시지 저장할 공간 (1024바이트)
    
    while(running) 
    {                           // running이 1인 동안 계속 실행
        memset(buffer, 0, sizeof(buffer));     // buffer를 0으로 초기화 (깨끗이 청소)
        int recv_len = recv(client_sock,       // 소켓에서 메시지 받기
                           buffer,             // 받은 데이터를 buffer에 저장
                           sizeof(buffer),     // 최대 1024바이트까지
                           0);                 // 옵션 (0=기본)
        
        if (recv_len <= 0) 
        {                   // 0 이하면 연결 끊김
            pthread_mutex_lock(&print_mutex);  //  출력 잠금
            printf("\n");                      // 줄바꿈
            print_time();                      // 시간 출력
            printf("클라이언트 연결 종료\n\n"); // 메시지 출력
            pthread_mutex_unlock(&print_mutex);//  출력 해제
            running = 0;                       // 프로그램 종료 신호
            break;                             // while 반복문 탈출
        }
        
        pthread_mutex_lock(&print_mutex);      //  출력 잠금
        printf("\n");                          // 줄바꿈
        print_time();                          // 시간 출력
        printf("클라이언트: %s\n\n", buffer);  // 받은 메시지 출력
        pthread_mutex_unlock(&print_mutex);    //  출력 해제
        
        if (strcmp(buffer, "quit") == 0) 
        {     // 메시지가 "quit"이면
            running = 0;                       // 프로그램 종료 신호
            break;                             // while 반복문 탈출
        }
    }
    
    return NULL;                               // 스레드 종료 (NULL 반환 필수)
}

// 메시지 보내기 스레드 (내가 입력한 거 보냄)
void* 
send_thread(void* arg) 
{       // 스레드 함수
    char message[1024];              // 보낼 메시지 저장할 공간
    
    // 첫 입력 프롬프트 출력// 
    pthread_mutex_lock(&print_mutex);      //  출력 잠금
    print_time();                          // 시간 출력
    printf("나: ");                        // "나: " 출력
    fflush(stdout);                        // 버퍼 비우기 (즉시 출력)
    pthread_mutex_unlock(&print_mutex);    //  출력 해제
    
    while(running) 
    {                                  // running이 1인 동안 반복
        fgets(message, sizeof(message), stdin);       // 키보드 입력 받기 (엔터까지)
        message[strcspn(message, "\n")] = 0;          // 엔터(\n)를 제거 (문자열 끝으로)
        
        if (strlen(message) == 0) 
        {                   // 빈 입력이면
            pthread_mutex_lock(&print_mutex);         //  출력 잠금
            print_time();                             // 시간 출력
            printf("나: ");                           // "나: " 출력
            fflush(stdout);                           // 즉시 출력
            pthread_mutex_unlock(&print_mutex);       //  출력 해제
            continue;                                 // 다음 반복으로
        }
        
        send(client_sock,                // 소켓을 통해 메시지 전송
             message,                    // 보낼 메시지
             strlen(message),            // 메시지 길이
             0);                         // 옵션 (0=기본)
        
        if (strcmp(message, "quit") == 0) 
        {           // "quit" 입력하면
            pthread_mutex_lock(&print_mutex);         //  출력 잠금
            printf("\n채팅 종료\n");                  // 종료 메시지
            pthread_mutex_unlock(&print_mutex);       //  출력 해제
            running = 0;                              // 프로그램 종료 신호
            break;                                    // while 반복문 탈출
        }
        
        // 다음 입력 프롬프트 출력
        if (running) 
        {                          // 아직 실행 중이면
            pthread_mutex_lock(&print_mutex);   //  출력 잠금
            print_time();                       // 시간 출력
            printf("나: ");                     // "나: " 출력
            fflush(stdout);                     // 즉시 출력
            pthread_mutex_unlock(&print_mutex); //  출력 해제
        }
    }
    
    return NULL;                        // 스레드 종료
}

// 메인 함수 (프로그램 시작점)
int 
main() 
{
    // 서버 소켓 생성
    int server_sock = socket(AF_INET,      // IPv4 인터넷 프로토콜 사용
                            SOCK_STREAM,   // TCP 방식 (신뢰성 있는 통신)
                            0);            // 프로토콜 자동 선택
    printf("서버 소켓 생성\n");
    
    // 소켓 재사용 옵션 설정 (서버 재시작 시 바로 사용 가능)
    int opt = 1;                           // 옵션 값 (1=활성화)
    setsockopt(server_sock,                // 설정할 소켓
               SOL_SOCKET,                 // 소켓 레벨 옵션
               SO_REUSEADDR,               // 주소 재사용 허용
               &opt,                       // 옵션 값 주소
               sizeof(opt));               // 옵션 크기
    
    // 서버 주소 설정
    struct sockaddr_in server_addr;               // 서버 주소 구조체
    server_addr.sin_family = AF_INET;             // IPv4 사용
    server_addr.sin_port = htons(8080);           // 포트 8080 (네트워크 바이트 순서로 변환)
    server_addr.sin_addr.s_addr = INADDR_ANY;     // 모든 IP에서 접속 허용 (0.0.0.0)
    
    // 바인딩 (소켓에 주소 할당)
    bind(server_sock,                             // 바인딩할 소켓
         (struct sockaddr*)&server_addr,          // 주소 정보 (형변환)
         sizeof(server_addr));                    // 주소 크기
    printf("포트 8080 바인딩\n");
    
    // 연결 대기 모드
    listen(server_sock,                           // 대기할 소켓
           5);                                    // 대기 큐 크기 (최대 5명)
    printf("클라이언트 대기 중...\n\n");
    
    //클라이언트 연결 수락
    struct sockaddr_in client_addr;               // 클라이언트 주소 저장
    socklen_t client_len = sizeof(client_addr);   // 주소 크기
    client_sock = accept(server_sock,             // 대기 중인 소켓
                        (struct sockaddr*)&client_addr,  // 클라이언트 주소 저장
                        &client_len);             // 주소 크기
    printf("클라이언트 연결됨!\n");
    printf("채팅 시작!\n");
    
    // 스레드 생성 (받기/보내기 동시 실행)
    pthread_t recv_tid, send_tid;                 // 스레드 ID 변수
    
    pthread_create(&recv_tid,                     // 스레드 ID 저장 위치
                   NULL,                          // 기본 속성 사용
                   receive_thread,                // 실행할 함수
                   NULL);                         // 함수에 전달할 인자 (없음)
    
    pthread_create(&send_tid,                     // 스레드 ID 저장 위치
                   NULL,                          // 기본 속성 사용
                   send_thread,                   // 실행할 함수
                   NULL);                         // 함수에 전달할 인자 (없음)
    
    // 스레드 종료 대기
    pthread_join(recv_tid, NULL);                 // 받기 스레드 종료 대기
    pthread_join(send_tid, NULL);                 // 보내기 스레드 종료 대기
    
    //정리 작업
    pthread_mutex_destroy(&print_mutex);          // 뮤텍스 해제
    close(client_sock);                           // 클라이언트 소켓 닫기
    close(server_sock);                           // 서버 소켓 닫기
    printf("서버 종료\n");
    
    return 0;                                     // 프로그램 정상 종료
}