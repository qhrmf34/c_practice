#include "server_function.h"
#include <limits.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    int session_id;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (argc != 4) 
    {
        fprintf(stderr, "main() : [Worker] 에러: 잘못된 인자 개수 (expected: 4, got: %d)\n", argc);
        fprintf(stderr, "main() : [Worker] 사용법: %s <session_id> <client_ip> <client_port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    ServerState state = {0};
    state.running = 1;
    state.log_fd = -1;
    log_init(&state);
    setup_signal_handlers(&state);
    char *endptr;
    errno = 0;
    long sid_long = strtol(argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || sid_long < 0 || sid_long > INT_MAX) 
    {
        fprintf(stderr, "main() : [Worker] 에러: 잘못된 session_id 형식 '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    session_id = (int)sid_long;
    int client_sock = 3;
    int optval;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(client_sock, SOL_SOCKET, SO_TYPE, &optval, &optlen) == -1) 
    {
        fprintf(stderr, "main() : [Worker #%d] 에러: FD 3이 유효한 소켓이 아님: %s\n", session_id, strerror(errno));
        return EXIT_FAILURE;
    }
    if (getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len) == -1) 
    {
        fprintf(stderr, "main() : [Worker #%d] 에러: getpeername() 실패: %s\n", session_id, strerror(errno));
        return EXIT_FAILURE;
    }
    printf("[Worker #%d (PID:%d)] exec() 성공!\n", session_id, getpid());
    child_process_main(client_sock, session_id, client_addr, &state);
    printf("[Worker #%d (PID:%d)] 정상 종료\n", session_id, getpid());
    log_close(&state);
    return 0;
}
