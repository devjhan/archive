#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <secure/_string.h>
#include <sys/errno.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>

#define BUFFER_LENGTH 256
typedef enum _statement
{
    EXIT = -1,
    NOT_INPUTTED = 0,
    INPUTTED = 1
} statement;

typedef struct
{
    int socket;
    volatile struct pollfd poller[2];
    volatile statement state; // 입력 쓰레드의 상태를 결정함
    pthread_mutex_t mutex;
    pthread_cond_t cond;
}shared_socket;

shared_socket shsock =
{
    .poller ={{.fd = STDIN_FILENO, .events = POLLIN},{.events = POLLIN | POLLHUP}},
    // poller[0] -> 표준 입력에 입력 이벤트가 일어나는 것을 감시
    // poller[1] -> 서버로부터의 입력, 또는 소켓 종료 이벤트를 감시
    .state = NOT_INPUTTED,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

void* run(void* args);
ssize_t filelen(FILE* fp); // 파일의 크기를 바이트 단위로 반환
int send_header(const char* filename, ssize_t length); // 헤더를 전송

int main(const int argc, const char* argv[])
{
    if (argc != 4)
    {
        printf("usage : %s <ip> <port> <path>\n", argv[0]);
        return -1;
    }
    // 매개변수 수가 잘못되었을 경우, 사용법을 출력하고 프로그램 종료

    struct sockaddr_in server_address =
    {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr(argv[1]),
        .sin_port = htons((ushort)strtol(argv[2], NULL, 10)),
    };
    // 서버 주소 구조체 초기화

    if (server_address.sin_addr.s_addr == INADDR_NONE)
    {
        printf("error : invalid ip address.\n");
        printf("format <ip> : xxx.xxx.xxx.xxx\n");
        return -1;
    }
    // ip 주소로 유효하지 않은 값이 들어온 경우, 프로그램 종료

    if (errno == ERANGE)
    {
        printf("error : invalid port number.\n");
        printf("format <port> : x (0 <= x <= 65535)\n");
        return -1;
    }
    // port 번호로 유효하지 않은 값이 들어온 경우, 프로그램 종료

    if ((shsock.socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("error : failed to create socket.\n");
        return -1;
    }
    // 소켓 생성에 실패한 경우, 프로그램 종료

    if (connect(shsock.socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        printf("error : failed to connect server.\n");
        close(shsock.socket);
        return -1;
    }
    // 서버와의 연결에 실패한 경우, 프로그램 종료

    shsock.poller[1].fd = shsock.socket;
    // 서버로부터의 입력, 또는 소켓 종료 이벤트를 감시하는 구조체의 필드 초기화

    FILE* fp = fopen(argv[3], "r");

    if (fp == NULL)
    {
        printf("error : failed to open %s", argv[3]);
        close(shsock.socket);
        return -1;
    }
    // 파일을 여는데 실패한 경우, 에러 반환

    ssize_t left_length = filelen(fp);

    if (send_header(argv[3], left_length))
    {
        printf("error : failed to send header.\n");
        close(shsock.socket);
        fclose(fp);
        return -1;
    }
    // "<filename>"length\n 형태의 헤더를 먼저 전송함. 에러 발생시 프로그램 종료

    ssize_t current_sent = 1;

    while (left_length > 0 && current_sent > 0 && !ferror(fp))
    {
        char buffer[BUFFER_LENGTH] = {0, };
        size_t byte_read = fread(buffer, 1, sizeof(buffer) < left_length ? sizeof(buffer) : left_length, fp);
        ssize_t total_sent = 0;

        while (total_sent < byte_read && (current_sent = send(shsock.socket, buffer + total_sent, byte_read - total_sent, 0)) > 0)
            total_sent += current_sent;

        left_length -= byte_read;
    }
    // 보낼 파일이 남아 있으며 파일에 에러가 생기지 않았으며 서버와의 연결에 문제가 없는 동안 인자로 받은 파일을 전송

    if (current_sent < 0 || ferror(fp))
    {
        printf("error : failed to send %s.\n", argv[3]);
        close(shsock.socket);
        fclose(fp);
        return -1;
    }
    // 서버와의 연결이 종료되거나 파일에 에러가 생긴 경우 프로그램 종료

    fclose(fp);

    pthread_t stdin_thread;

    if (pthread_create(&stdin_thread, NULL, run, NULL))
    {
        printf("error : failed to create stdin thread.\n");
        close(shsock.socket);
        return -1;
    }
    // 입력 쓰레드 생성에 실패한 경우, 프로그램 종료

    ssize_t received = 1;

    while (received > 0)
    {
        int inputted = poll((struct pollfd*)shsock.poller, 2, -1);
        // 표준 입력 또는 서버로부터 받는 것이 생길때까지 무한대기
        if (inputted > 0)
        {
            if (shsock.poller[0].revents & POLLIN)
            {
                shsock.state = INPUTTED;
                pthread_cond_signal(&shsock.cond);
                // 표준 입력에 입력이 감지된 경우, 조건변수를 업데이트하고 입력 쓰레드를 깨움
            }
            if (shsock.poller[1].revents & (POLLIN | POLLHUP))
            {
                char out[BUFFER_LENGTH] = { 0, };
                received = recv(shsock.socket, out, sizeof(out), 0);
                printf("%s", out);
                // 서버로부터 받은 메시지가 있을 경우 또는 소켓의 연결이 끊겼을 경우, out 버퍼에 받고 출력
            }
        }
        else if (inputted < 0)
        {
            printf("error!\n");
            shsock.state = EXIT;
            pthread_cond_signal(&shsock.cond);
            pthread_join(stdin_thread, NULL);
            close(shsock.socket);
            return -1;
            //poll 함수가 에러를 반환한 경우, 프로그램 종료
        }

    }
    // 수신 루프. 불완전한 수신은 서버가 처리함. 정상적으로 종료되었을 경우 서버는 shutdown 함수를 호출하며 통신을 종료함.
    shsock.state = EXIT;
    pthread_cond_signal(&shsock.cond);
    pthread_join(stdin_thread, NULL);
    close(shsock.socket);
    // 조건변수를 EXIT로 수정하고 시그널을 보내 입력 쓰레드를 종료시킴

    if (received < 0)
    {
        printf("error : failed to receive output.\n");
        return -1;
    }
    // 소켓의 연결에 문제가 생겨 종료된 경우, 에러 출력
    printf("connection finished.\n");
    return 0;
    // 정상 종료
}

void* run(void* args)
{
    while (shsock.state > EXIT)
    {
        pthread_mutex_lock(&shsock.mutex);

        while (!shsock.state)
        {
            pthread_cond_wait(&shsock.cond, &shsock.mutex);
        }
        // state가 NOT_INPUTTED인 동안 무한 대기

        char buffer[BUFFER_LENGTH] = { 0, };

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
        size_t length = strlen(buffer);
        ssize_t sent = send(shsock.socket, buffer, length - 1, 0);
        int temp = 1;

        while (sent < length && temp > 0)
        {
            temp = send(shsock.socket, buffer + sent, length - sent, 0);
            sent += temp;
        }
        if (temp < 0)
        {
            printf("error : failed to send clients input.\n");
            close(shsock.socket);
            break;
        }
        shsock.state = shsock.state > EXIT ? NOT_INPUTTED : EXIT;
        // 프로그램의 상태가 NOT_INPUTTED인 경우, 다시 대기. EXIT인 경우, 그대로 쓰레드 종료
        pthread_mutex_unlock(&shsock.mutex);
    }
    pthread_mutex_unlock(&shsock.mutex);
    return NULL;
}

ssize_t filelen(FILE* fp)
{
    fseek(fp, 0, SEEK_END);
    ssize_t file_size = ftell(fp);
    rewind(fp);
    return file_size;
}

int send_header(const char* filename, ssize_t length)
{
    char header[BUFFER_LENGTH];
    snprintf(header, sizeof(header), "\"%s\"%ld\n", filename, length);
    // 헤더의 포맷을 생성
    ssize_t total_sent = 0, sent = 0;

    while (total_sent < sizeof(header) && (sent = send(shsock.socket, header + sent, sizeof(header) - total_sent, 0)) > 0)
    {
        total_sent += sent;
    }
    // 모든 헤더를 전송하지 않았으며 연결이 유지되는 동안 헤더 전송
    return send < 0; // 연결에 에러가 발생했는지 아닌지를 반환
}