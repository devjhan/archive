#include <sys/errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define BUFFER_LENGTH 256
#define format_by(name, size, style, ...) \
    char name[size];\
    snprintf(name, size, style, ##__VA_ARGS__)\

typedef enum _resource
{
    CLIENT = 0,
    FP,
    SOURCE_CODE,
    OBJECT_CODE,
} resource;

typedef struct
{
    char path[BUFFER_LENGTH];
    char filename[BUFFER_LENGTH];
    char ext[BUFFER_LENGTH];
    ssize_t length;
}metadata;
// 클라이언트가 인자로 보낸 프로그램의 메타데이터를 저장하는 구조체

int parse_header(const char* header, metadata* metadata); // 파일을 전송받기 전, 전송받은 헤더를 파싱하는 함수
int complete_send(const int socket, const char* message, const size_t len, const int flag);
void* run(void* args); // 쓰레드 함수

int main(const int argc, const char* argv[])
{
    if (argc != 2)
    {
        printf("usage :  ./server <port>.\n");
        return -1;
    }
    struct sockaddr_in server_address =
    {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons((ushort)strtol(argv[1], NULL, 10))
    };
    // 서버 주소 구조체를 초기화

    int server_socket;

    if (errno == ERANGE)
    {
        printf("error : invalid port number.\n");
        printf("format <port> : x (0 <= x <= 65535)\n");
        return -1;
    }
    // 포트 넘버 유효성 체크

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("error : failed to create server socket.\n");
        return -1;
    }
    // 서버 소켓 생성에 실패했을 경우, 에러 반환

    if (bind(server_socket, (struct sockaddr* )&server_address, sizeof(server_address)) < 0 || listen(server_socket, 5) < 0)
    {
        printf("error : error occured while binding and listening\n");
        close(server_socket);
        return -1;
    }
    // 서버 소켓 바인딩 또는 리슨에 실패했을 경우, 소켓을 닫고 에러 반환

    while (1)
    {
        struct sockaddr_in client_address;
        socklen_t client_length  = sizeof(client_address);

        int client_socket = accept(server_socket, (struct sockaddr *) &client_address, &client_length);

        if (client_socket < 0)
        {
            printf("error : failed to accept request.\n");
            continue;
        }
        // 클라이언트 소켓을 받는데 실패했을 경우, 연결을 포기하고 넘어감

        pthread_t thread;

        if (pthread_create(&thread, NULL, run, &client_socket))
        {
            complete_send(client_socket, "error : failed to create thread.\n", 33, 0);
            close(client_socket);
            // 쓰레드 생성에 실패한 경우, 클라이언트와의 접속을 종료
        }
        else
        {
            pthread_detach(thread);
            // 쓰레드 생성에 성공한 경우, pthread_detach 함수를 통해 쓰레드가 종료될 경우 자동으로 자원을 회수하게 만듦
        }
    }
    close(server_socket);
    return 0;
}

void* run(void* args)
{
    u_char resource_status = 0b00000001;
    // 어떤 자원이 할당되어 있는지를 비트마스킹으로 관리. client_socket는 유효하다고 가정함.

    const int client_socket = *(int *)args;
    char header[BUFFER_LENGTH];

    ssize_t header_rcvd = 0, temp = 0;
    // 파일을 전송받기 이전, <filepath>length\n 형식의 헤더를 먼저 전송받음.

    while (header[header_rcvd - 1] < sizeof(header) && (temp = recv(client_socket, header + header_rcvd, sizeof(header) - header_rcvd, 0)) > 0)
    {
        header_rcvd += temp;
    }
    // <filepath>length\n 형식의 헤더를 온전히 받지 못헀을 경우, 다시 전송받기 위해 시도

    if (temp < 0) goto KILL_RESOURCE;
    // 전송받은 헤더에 문제가 있을 경우, 연결을 종료함
    metadata metadata = {"", "", "", 0};

    if (parse_header(header, &metadata)) goto KILL_RESOURCE;
    // 헤더를 파싱하여 파일의 경로 <path>, 파일 경로와 확장자를 제외한 파일명 <filename>, 확장자 <ext>, 파일 길이 <length>
    // 를 포함한 메타 데이터를 획득. 실패했을 경우 에러 반환.

    srand(time(NULL));

    format_by(new_file, BUFFER_LENGTH * 2, "%s_%d", metadata.filename, rand());
    format_by(new_file_ext, BUFFER_LENGTH * 2, "%s.%s", new_file, metadata.ext);
    format_by(gen_file, BUFFER_LENGTH * 2, "./%s", new_file_ext);
    // 메타 데이터를 활용하여 새롭게 생성할 파일의 이름을 생성함. ./<filname>_rand().<ext> 형식
    FILE* fp = fopen(gen_file, "w");

    if (fp == NULL) goto KILL_RESOURCE;
    resource_status |= (1 << FP);
    resource_status |= (1 << SOURCE_CODE);
    //파일 생성에 실패한 경우, 에러 메시지를 보내고 연결을 종료함

    ssize_t left_length = metadata.length;

    while (left_length > 0) // 남은 파일 길이가 0 이상인 경우, 계속 작성
    {
        char buffer[BUFFER_LENGTH];

        const ssize_t recvd = recv(client_socket, buffer, BUFFER_LENGTH < left_length ? BUFFER_LENGTH : left_length, 0);

        if (recvd < 0) goto KILL_RESOURCE;
        // recv 함수가 0 이하의 값을 반환한 경우, 연결이 종료되었다고 판단하고 자원을 정리함
        const size_t bytes_written = fwrite(buffer, 1, recvd, fp);

        if (bytes_written != recvd) goto KILL_RESOURCE;
        // fwrite 함수로 파일에 작성한 파일 길이가 받은 메시지의 길이보다 짧을 경우, 에러가 있다고 판단하고 연결을 종료

        left_length -= bytes_written;
        // 남은 길이에 파일에 작성한 길이를 뺌
    }
    // 생성한 파일에 전송받은 파일의 내용을 복사함

    fclose(fp);
    resource_status &= 0b11111101;

    format_by(cmd, BUFFER_LENGTH * 3, "gcc -o %s %s", new_file, new_file_ext);
    // gcc -o <filename>_rand <filename>_rand.<ext> 형식의 컴파일 명령어 생성

    if (system(cmd)) goto KILL_RESOURCE;
    resource_status |= (1 << OBJECT_CODE);
    // 컴파일 에러 발생시 에러 반환하고 연결 종료
    pid_t pid = fork();

    if (pid == -1) goto KILL_RESOURCE;
    // 자식 프로세스 생성 실패 시, 자원을 정리하고 쓰레드 종료
    if (pid == 0)
    {
        // 자식 프로세스에서 수행할 작업
        if (dup2(client_socket, STDOUT_FILENO) == -1 || dup2(client_socket, STDERR_FILENO) == -1 || dup2(client_socket, STDIN_FILENO) == -1) goto KILL_RESOURCE;
        // 서버의 표준 입출력을 클라이언트 표준 입출력으로 리다이렉트. 실패할 경우, 자원을 정리하고 프로세스를 종료함

        format_by(execution, BUFFER_LENGTH * 2, "stdbuf -o0 ./%s", new_file);
        //또는 format_by(execution, BUFFER_LENGTH * 2, "./%s", new_file);

        int prog_return = system(execution);

        if (prog_return) printf("error : failed to execute process.\n");

        remove(new_file_ext);
        remove(new_file);
        close(client_socket);
        exit(prog_return);
        //클라이언트가 전송한 프로그램을 실행한 뒤, 자원 정리 후 프로그램의 리턴값을 반환
    }
    wait(&pid);
    // 자식 프로세스를 호출하고, 쓰레드가 사용한 자원을 정리한 뒤 쓰레드를 종료. 다시 main문에서 다른 클라이언트의 요청을 대기함.

    KILL_RESOURCE:
        if (resource_status & (1 << FP)) fclose(fp);
        if (resource_status & (1 << SOURCE_CODE)) remove(new_file_ext);
        if (resource_status & (1 << OBJECT_CODE)) remove(new_file);
        if (resource_status & (1 << CLIENT)) close(client_socket);
    return NULL;
}

int parse_header(const char* header, metadata* metadata)
{
    if (sscanf(header, "\"%[^\"]\"%ld\n", metadata->path, &metadata->length) != 2 ||
        sscanf(metadata->path, "%[^.].%s", metadata->filename, metadata->ext) != 2)
        return 1; // 파싱에 실패했을 경우, 1 반환

    const char* temp = strrchr(metadata->filename, '/');
    if (temp != NULL) strcpy(metadata->filename, temp + 1);
    return 0; // 파싱에 성공했을 경우, 0 반환
}

int complete_send(const int socket, const char* message, const size_t len, const int flag)
{
    size_t total_sent = 0;
    ssize_t current_sent = 0;

    while (total_sent < len && (current_sent = send(socket, message + total_sent, len - total_sent, flag)) > 0)
    {
        total_sent += current_sent;
    }
    // 모든 message를 전송하거나 send 함수가 0 이하의 값을 반환(에러 또는 연결 종료)할 때까지 반복.
    // 반환값은 current_sent 변수를 사용해 에러가 있을 경우 1, 없을 경우 0을 반환.
    return current_sent < 0;
}