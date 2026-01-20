#define _POSIX_C_SOURCE 200809L
#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>

// 전역 변수
static volatile sig_atomic_t server_running = 1;
static int total_forks = 0;
static int total_execs = 0;
static int fork_errors = 0;
static int exec_errors = 0;
static int zombie_reaped = 0;
static time_t start_time;
static pid_t g_parent_pid = 0;

// Worker PID 추적
#define MAX_WORKERS 10000
static pid_t worker_pids[MAX_WORKERS];
static int worker_count = 0;  // volatile sig_atomic_t 제거 (메인에서만 증가, SIGCHLD에서만 감소)

// === async-signal-safe 헬퍼 함수들 ===
// 안전한 문자열 출력 (async-signal-safe)
static void 
safe_write(const char *msg)
{
    size_t len = 0;
    while (msg[len]) len++;
    write(STDERR_FILENO, msg, len);
}

// 정수를 문자열로 변환 후 출력 (async-signal-safe)
static void
safe_write_int(int num)
{
    char buf[32];
    int i = 0;
    int is_negative = 0;
    
    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }
    if (num == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while (num > 0)
        {
            buf[i++] = '0' + (num % 10);
            num /= 10;
        }
    }
    if (is_negative)
    {
        buf[i++] = '-';
    }
    // 역순으로 출력
    while (i > 0)
    {
        write(STDERR_FILENO, &buf[--i], 1);
    }
}

// SIGCHLD 핸들러 - 좀비 프로세스 회수 (async-signal-safe)
static void 
sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno; //메인로직 도중에 시그널 핸들러가 끼어들어 값이 바뀔수 있기에 복원
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        zombie_reaped++;
        // Worker PID 배열에서 제거 (SIGCHLD에서만 worker_count 감소) -> 부모가 모든자식에게 SIGTERM 보내기 위해 사용
        for (int i = 0; i < worker_count; i++)
        {
            if (worker_pids[i] == pid)
            {
                // 마지막 요소와 교체
                worker_pids[i] = worker_pids[worker_count - 1];
                worker_count--;  // SIGCHLD에서만 감소
                break;
            }
        }
        // write()를 사용한 안전한 로깅 (async-signal-safe)
        if (WIFEXITED(status)) 
        {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 127) 
            {
                safe_write("[SIGCHLD] Worker 실행 실패 (파일 없음)\n");
            } 
            else if (exit_code > 0) 
            {
                safe_write("[SIGCHLD] Worker 로직 에러 종료\n");
            } 
            else 
            {
                safe_write("[SIGCHLD] Worker 정상 종료\n");
            }
        }
        else if (WIFSIGNALED(status)) //SIGSEGV, SIGABRT등 에러 발생시(자식 raise시 이쪽으로)
        {
            // 시그널로 종료 (crash)
            int sig = WTERMSIG(status);
            safe_write("[SIGCHLD] Worker 시그널 종료 (PID: ");
            safe_write_int(pid);
            safe_write(", 시그널: ");
            safe_write_int(sig);
            safe_write(") - 좀비 회수: ");
            safe_write_int(zombie_reaped);
            safe_write("\n");
        }
        else
        {
            safe_write("[SIGCHLD] Worker 상태 변경 (기타): PID ");
            safe_write_int(pid);
            safe_write("\n");
        }
    }
    errno = saved_errno;
}

// 서버 종료 signal handler (async-signal-safe)
static void 
shutdown_handler(int signo)
{
    (void)signo;
    // 부모 프로세스만 처리. fork되고 SIGINT를 무시하기 전 찰나에 자식이 들어온다면
    if (getpid() != g_parent_pid)
    {
        return;  // 자식은 원래로 복귀
    }
    // 플래그만 설정
    // 로그는 메인 루프 밖에서 출력
    server_running = 0; //while문 빠져나감
}
/*
 * fork-exec 방식으로 worker 프로세스 생성
 * 
 * 자식 프로세스는 exec 성공 시 반환하지 않고 worker 프로그램으로 대체됨
 */
static int
fork_and_exec_worker(int serv_sock, int clnt_sock, int session_id, struct sockaddr_in *clnt_addr)
{
    pid_t pid;
    char fd_str[32];
    char session_str[32];
    char ip_str[INET_ADDRSTRLEN];
    char port_str[32];

    //exec 인자 준비 
    // FD를 문자열로 변환 fd_str에 clnt_sock 저장
    snprintf(fd_str, sizeof(fd_str), "%d", clnt_sock);
    //  session_id를 문자열로 변환
    snprintf(session_str, sizeof(session_str), "%d", session_id);
    //IP 주소를 문자열로 변환
    if (inet_ntop(AF_INET, &clnt_addr->sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        log_message(LOG_ERROR, "inet_ntop() 실패: %s (session #%d)", 
                   strerror(errno), session_id);
        return -1;
    }
    //포트를 문자열로 변환
    snprintf(port_str, sizeof(port_str), "%d", ntohs(clnt_addr->sin_port));
    // 시그널 블로킹 (critical section) - fork중간에 SIGINT 올 경우 부모 자식중 하나만 종료 처리 될 수 있음
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD); //worker_count race condition 방지
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);
    
    //  fork 
    pid = fork();
    
    if (pid == -1)
    {
        //  fork 실패 시그널 블로킹 상태이기에 프로세스, 메모리만 체크
        fork_errors++;
        if (errno == EAGAIN) //프로세스 수 제한
        {
            log_message(LOG_ERROR, "fork() 실패: 프로세스 리소스 한계 (EAGAIN)");
            log_message(LOG_ERROR, "통계: fork 성공=%d, fork 실패=%d, exec 성공=%d, exec 실패=%d",
                       total_forks, fork_errors, total_execs, exec_errors);
        }
        else if (errno == ENOMEM) //메모리 부족
        {
            log_message(LOG_ERROR, "fork() 실패: 메모리 부족 (ENOMEM)");
        }
        else
        {
            log_message(LOG_ERROR, "fork() 실패: %s (errno: %d)", 
                       strerror(errno), errno);
        }
        // 시그널 언블록
        sigprocmask(SIG_SETMASK, &old_set, NULL);
        return -1;
    }
    // 자식 프로세스 - exec 수행 
    if (pid == 0)
    {
        setup_child_signal_handlers();

        // 시그널 언블록 -- SIGINT를 무시하기 전에 신호가 올경우 shutdown_handler로 가서 return
        sigprocmask(SIG_SETMASK, &old_set, NULL);

        // 서버 소켓 닫기 (자식에게 불필요)
        close(serv_sock);  // 전역 변수 serv_sock을 닫음 (run_server에서 정의)
        
        //  FD_CLOEXEC 플래그 제거 (exec 후에도 FD 유지)
        int flags = fcntl(clnt_sock, F_GETFD);
        if (flags == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_GETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        flags &= ~FD_CLOEXEC;  // FD_CLOEXEC 제거 - fd가 넘어가도록
        if (fcntl(clnt_sock, F_SETFD, flags) == -1)
        {
            fprintf(stderr, "[자식 #%d] fcntl(F_SETFD) 실패: %s\n", 
                    session_id, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        // SIGINT 핸들러 리셋 sigint처리는 부모만 ! sigterm의 경우 자식내에서 signal_handler로 처리
        // 부모는 SIGINT시 shutdown_handler()실행 - server_running 0되며 while문 탈출(flag 기반)
        // 자식은 정상실행후 종료
        signal(SIGINT, SIG_IGN); //SIGINT는 무시
        signal(SIGTERM, SIG_DFL); //SIGTERM은 기본 동작으로 리셋

        //  exec 수행
        char *const argv[] = {
            "./worker",      // worker 프로그램 경로
            fd_str,          // argv[1]: client_sock FD
            session_str,     // argv[2]: session_id
            ip_str,          // argv[3]: client IP
            port_str,        // argv[4]: client port
            NULL
        };
        execvp("./worker", argv);

        // === exec 실패 시 (여기 도달하면 exec 실패) ===
        fprintf(stderr, "[자식 #%d] exec() 실패: %s (errno: %d)\n", 
                session_id, strerror(errno), errno);
        
        // 상세 에러 메시지
        if (errno == ENOENT)
        {
            fprintf(stderr, "[자식 #%d] worker 실행파일을 찾을 수 없음 (ENOENT)\n", 
                    session_id);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "[자식 #%d] worker 실행 권한 없음 (EACCES)\n", 
                    session_id);
        }
        else if (errno == ENOMEM)
        {
            fprintf(stderr, "[자식 #%d] 메모리 부족 (ENOMEM)\n", 
                    session_id);
        }
        // exec 실패 시 특수 exit code 반환 (부모가 감지 가능)
        _exit(127);  // exec 실패 표준 exit code
    }
    
    // 부모 프로세스 - 후처리 
    
    // fork 성공 카운트
    total_forks++;
    
    // Worker PID 저장 (SIGCHLD 블록된 상태)
    if (worker_count < MAX_WORKERS)
    {
        worker_pids[worker_count++] = pid;  //  메인에서만 증가
    }
    else
    {
        log_message(LOG_WARNING, "Worker PID 배열 가득참 (MAX_WORKERS=%d)", MAX_WORKERS);
    }

    // 클라이언트 소켓 닫기 (부모는 더 이상 사용하지 않음)
    close(clnt_sock);
    
    // 시그널 언블록
    sigprocmask(SIG_SETMASK, &old_set, NULL);
    
    // 로그 출력
    log_message(LOG_INFO, "Worker 프로세스 생성 (PID: %d, Session #%d)", 
               pid, session_id);
    log_message(LOG_DEBUG, "fork 성공=%d, exec 대기 중...", total_forks);
    
    // exec 성공 여부는 SIGCHLD에서 확인
    // exit code가 127이면 exec 실패
    return 0;  // 부모 프로세스는 성공 반환
}

void 
run_server(void)
{
    int serv_sock, clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t addr_size;
    int session_id = 0;
    struct pollfd fds[1];
    int poll_result;
    
    start_time = time(NULL);
    g_parent_pid = getpid();

    log_message(LOG_INFO, "SIGPIPE 무시 설정 완료");

    // 부모용 Stack trace 설정
    setup_parent_signal_handlers();
    log_init();

    // SIGCHLD 핸들러 설정
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGCHLD) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    
    // SIGINT, SIGTERM 핸들러
    struct sigaction shutdown_act;
    shutdown_act.sa_handler = shutdown_handler; 
    sigemptyset(&shutdown_act.sa_mask);
    shutdown_act.sa_flags = 0;
    
    if (sigaction(SIGINT, &shutdown_act, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGINT) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGTERM, &shutdown_act, NULL) == -1)
    {
        log_message(LOG_ERROR, "sigaction(SIGTERM) 설정 실패: %s", strerror(errno));
        exit(1);
    }
    log_message(LOG_INFO, "=== Multi-Process Echo Server (fork-exec) 시작 ===");
    log_message(LOG_INFO, "Port: %d", PORT);
    
    // 서버 소켓 생성
    serv_sock = create_server_socket();
    if (serv_sock == -1)
    {
        log_message(LOG_ERROR, "서버 소켓 생성 실패");
        exit(1);
    }
    log_message(LOG_INFO, "클라이언트 연결 대기 중 (poll 사용, fork-exec 방식)");
    // poll 설정
    fds[0].fd = serv_sock;
    fds[0].events = POLLIN;

    // 메인 루프
    while (server_running)
    {
        // poll로 신규 연결 대기 (1초 timeout)
        poll_result = poll(fds, 1, 1000);
        
        if (poll_result == -1)
        {   //EINTR - 시그널 신호 감지 - 자식 프로세스 종료되어 SIGCHLD 시그널 날아올 경우 sigchld_handler에서 처리 후 다시 연결 대기
            //SIGIN로 부모가 종료될 경우 shutdown_handler에서 처리 후 server_running=0; poll문으로 돌아와 continue 후 while문 벗어남
            //데이터 없으니 나중에 다시 시도
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                // 시그널 인터럽트 (정상) - 재시도
                continue;
            }
            // 심각한 에러 - 종료
            log_message(LOG_ERROR, "poll() 실패: %s (errno: %d)", 
                       strerror(errno), errno);
            break;
        }
        else if (poll_result == 0)
        {
            // 타임아웃 (신규 연결 없음) - 재시도
            continue;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            log_message(LOG_ERROR, "poll 에러 이벤트 발생: 0x%x", fds[0].revents);
            
            if (fds[0].revents & POLLERR) //소켓 비동기 I/O 에러
            {
                log_message(LOG_ERROR, "POLLERR: 소켓 에러");
                continue;
            }
            if (fds[0].revents & POLLHUP) //상대방이 연결을 닫음
            {
                log_message(LOG_ERROR, "POLLHUP: 연결 끊김");
                continue;
            }
            if (fds[0].revents & POLLNVAL)// POLL에 등록된 fd가 유효하지 않음(서버소켓이 문제)
            {
                log_message(LOG_ERROR, "POLLNVAL: 잘못된 FD");
                // 서버 소켓이 유효하지 않으므로 서버 종료
                break;
            }
        }
        // POLLIN 이벤트 확인
        if (fds[0].revents & POLLIN)
        {
            addr_size = sizeof(clnt_addr);
            clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &addr_size);
            if (clnt_sock == -1)
            {
                // 서버 종료 중
                if (!server_running)
                {
                    log_message(LOG_INFO, "종료 시그널로 인한 accept() 중단");
                    break;
                }
                
                // EINTR - 시그널 인터럽트 (재시도) - SIGCHLD 시그널
                if (errno == EINTR)
                {
                    log_message(LOG_DEBUG, "accept() interrupted (EINTR), 재시도");
                    continue;
                }
                
                // EAGAIN/EWOULDBLOCK 
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    log_message(LOG_DEBUG, "accept() 일시적으로 불가 (EAGAIN), 재시도");
                    continue;
                }
                
                // 기타 심각한 에러
                log_message(LOG_ERROR, "accept() 실패: %s (errno: %d)", 
                           strerror(errno), errno);
                continue;
            }
            //  shutdown 중 새 연결 거부
            if (!server_running)
            {
                log_message(LOG_INFO, "종료 중이므로 새 연결 거부");
                close(clnt_sock);
                break;
            }
            // 연결 수락 성공
            session_id++;
            log_message(LOG_INFO, "새 연결 수락: %s:%d (Session #%d, fd: %d)",
                       inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port), 
                       session_id, clnt_sock);
            
            // === fork-exec로 worker 생성 ===
            int result = fork_and_exec_worker(serv_sock, clnt_sock, session_id, &clnt_addr);
            
            if (result == -1)
            {
                // fork 실패 - 클라이언트 소켓을 직접 닫아야 함
                close(clnt_sock);
                log_message(LOG_ERROR, "Worker 생성 실패 (Session #%d)", session_id);
            }
            // fork 성공 (exec 성공 여부는 SIGCHLD에서 확인)
            // clnt_sock은 이미 fork_and_exec_worker에서 닫힘
        }
    }
    
    // 종료 처리 (시그널 핸들러 외부에서 로그 출력) 
    time_t end_time = time(NULL);
    
    log_message(LOG_INFO, "서버 종료 시그널 수신");
    log_message(LOG_INFO, "총 실행 시간: %ld초", end_time - start_time);
    log_message(LOG_INFO, "성공한 fork: %d개", total_forks);
    log_message(LOG_INFO, "실패한 fork: %d개", fork_errors);
    log_message(LOG_INFO, "회수한 좀비: %d개", zombie_reaped);
    
    // 모든 Worker에게 SIGTERM 전송
    int initial_count = worker_count;  // 시작 시점의 worker 수 저장
    log_message(LOG_INFO, "활성 Worker %d개에게 SIGTERM 전송", initial_count);
    
    for (int i = 0; i < initial_count; i++)
    {
        //  worker_count는 SIGCHLD에서 감소할 수 있으므로 저장된 PID만 사용
        if (i < worker_count && kill(worker_pids[i], SIGTERM) == 0)
        {
            log_message(LOG_DEBUG, "SIGTERM 전송: PID %d", worker_pids[i]);
        }
    }
    
    // 5초동안 모든 자식프로세스 사라졌는지 검사
    log_message(LOG_INFO, "Worker 정상 종료 대기 중 (5초)...");
    time_t wait_start = time(NULL);
    
    while (time(NULL) - wait_start < 5)
    {
        if (worker_count == 0)  // SIGCHLD에서 감소시킴
        {
            log_message(LOG_INFO, "모든 Worker 정상 종료 완료");
            break;
        }
    }
    
    int graceful_exits = initial_count - worker_count;
    log_message(LOG_INFO, "정상 종료: %d개, 남은 Worker: %d개",
               graceful_exits, worker_count);
    
    // 남은 Worker 강제 종료
    if (worker_count > 0)
    {
        log_message(LOG_WARNING, "남은 Worker %d개 강제 종료 (SIGKILL)", worker_count);
        
        //  현재 worker_count만큼만 순회
        int remaining = worker_count;
        for (int i = 0; i < remaining; i++)
        {
            if (i < worker_count && kill(worker_pids[i], 0) == 0)
            {
                log_message(LOG_WARNING, "SIGKILL 전송: PID %d", worker_pids[i]);
                kill(worker_pids[i], SIGKILL);
            }
        }
        
        // SIGKILL 후 잠깐 대기
        usleep(100000);  // 100ms
    }
    
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "서버 소켓 닫기 완료");
    
    //  최종 회수 (WNOHANG 사용)
    log_message(LOG_INFO, "최종 좀비 회수 중...");
    int final_count = 0;
    pid_t pid;
    
    //  WNOHANG 사용 (블로킹 방지)
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        final_count++;
    }
    
    //  에러 체크
    if (pid == -1 && errno != ECHILD)
    {
        log_message(LOG_ERROR, "waitpid() 에러: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "최종 회수: %d개", final_count);
    log_message(LOG_INFO, "총 회수된 좀비: %d개", zombie_reaped);
    log_message(LOG_INFO, "남은 활성 Worker: %d개", worker_count);
    
    if (worker_count > 0)
    {
        log_message(LOG_WARNING, "경고: 회수하지 못한 Worker가 있을 수 있음");
    }
    
    print_resource_limits();
    
    if (close(serv_sock) == -1)
    {
        log_message(LOG_ERROR, "close(serv_sock) 실패: %s", strerror(errno));
    }
    
    log_message(LOG_INFO, "서버 정상 종료 완료");
}

// [사용자] Ctrl+C - 서버
//     ↓
// [커널] SIGINT → 서버
//     ↓
// [서버] poll() 중단 → EINTR
//     ↓
// [서버] shutdown_handler() 실행
//     server_running = 0
//     ↓
// [서버] poll() 리턴 (result = -1, errno = EINTR)
//     ↓
// [서버] if (errno == EINTR) continue;
//     ↓
// [서버] while (server_running)  // 0 → 탈출
//     ↓
// [서버] 모든 자식에게 SIGTERM 전송
//     ↓
// [자식]  worker_term_handler() 실행
//      worker_running=0
//     ↓
// [자식] if (errno == EINTR) continue;
//     ↓
// [자식] while (worker_running)  // 0 → 탈출 -> SIGCHLD 시그널
//     ↓
// [서버] waitpid - 자식이 끝나기를 대기.


// [사용자] 특정자식 - KILL
//     ↓
// [자식]  worker_term_handler() 실행
//      worker_running=0
//     ↓
// [자식] if (errno == EINTR) continue;
//     ↓
// [자식] while (worker_running)  // 0 → 탈출 -> SIGCHLD 시그널
//     ↓
// [커널] SIGCHLD → 서버
//     ↓
// [서버] poll() 중단 → EINTR
//     ↓
// [서버] sigchld_handler() 실행
//     zombie_reaped++
//     // server_running은 그대로 1
//     ↓
// [서버] poll() 리턴 (result = -1, errno = EINTR)
//     ↓
// [서버] if (errno == EINTR) continue;
//     ↓
// [서버] while (server_running)  // 1 → 계속
//     ↓
// [서버] poll() 다시 호출
