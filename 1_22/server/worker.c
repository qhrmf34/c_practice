#include "server_function.h"
#include <limits.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    int session_id;                                 // 세션 번호 저장 변수
    struct sockaddr_in client_addr;                 // 클라이언트 주소 정보
    socklen_t addr_len = sizeof(client_addr);       // 주소 구조체 크기
    if (argc != 4)                                  // 인자 개수 확인 (세션ID, IP, 포트)
    {
        fprintf(stderr, "main() : [Worker] 에러: 잘못된 인자 개수 (expected: 4, got: %d)\n", argc);
        fprintf(stderr, "main() : [Worker] 사용법: %s <session_id> <client_ip> <client_port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    ServerState state = {0};                        // 워커 전용 상태 구조체 생성
    state.running = 1;                              // 워커 실행 플래그 활성화
    state.log_fd = -1;                              // 로그 FD 초기화
    log_init(&state);                               // 워커 로그 시스템 초기화
    setup_signal_handlers(&state);                  // 워커용 시그널 핸들러 등록
    char *endptr;
    errno = 0;
    long sid_long = strtol(argv[1], &endptr, 10);   // 문자열 세션 ID를 숫자로 변환
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX) 
    {
        fprintf(stderr, "main() : [Worker] 에러: 잘못된 session_id 형식 '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    session_id = (int)sid_long;
    int client_sock = 3;                            // 부모가 dup2로 넘겨준 3번 FD 사용
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1)      // FD 3번이 실제 소켓인지 검증
    {
        fprintf(stderr, "main() : [Worker #%d] 에러: FD 3이 유효한 소켓이 아님: %s\n", session_id, strerror(errno));
        return EXIT_FAILURE;
    }
    if (getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len) == -1) // 소켓을 통해 상대방 정보 획득
    {
        fprintf(stderr, "main() : [Worker #%d] 에러: getpeername() 실패: %s\n", session_id, strerror(errno));
        return EXIT_FAILURE;
    }
    printf("[Worker #%d (PID:%d)] exec() 성공!\n", session_id, getpid());
    child_process_main(client_sock, session_id, client_addr, &state);
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    log_close(&state);                              // 로그 파일 닫기
    return 0;                                       // 워커 프로세스 종료
}
