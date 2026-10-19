#include "icslab2_net.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    /*時間計測用*/
    unsigned int sec;
    int nsec;
    double time;
    unsigned int w_usec;
    struct timespec start_time, end_time;

    /*ファイル出力用*/
    char *filename = NULL;
    int fd = 1;

    int yes = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
               &yes, sizeof(yes));

    struct sockaddr_in serverAddr = {0};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(TCP_SERVER_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(server_sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(server_sock, 5);

    char buf[BUF_LEN];
    int n;

    //ファイルを開く
    if(argc > 1){
        filename = argv[1];
        fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fd < 0) {
            perror("open");
            return 1;
        }
    }else{
        printf("Usage: %s [o_filename]\n", argv[0]);
        return 0;
    }

    //時間計測開始
    clock_gettime(CLOCK_REALTIME, &start_time);

    // ------------------------------------------
    // ソケットを開いてnode3に接続を行う
    // ------------------------------------------
    struct sockaddr_in connectAddr;
    memset(&connectAddr, 0, sizeof(connectAddr));
    connectAddr.sin_family = AF_INET;
    connectAddr.sin_port = htons(TCP_SERVER_PORT);
    if(inet_pton(AF_INET, "172.21.0.30", &connectAddr.sin_addr) != 1) {
        fprintf(stderr, "invalid address\n");
        return 1;
    }

    int connect_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(connect_sock < 0) {
        perror("socket");
        return  1;
    }
    if(connect(connect_sock, (struct sockaddr *)&connectAddr, sizeof(connectAddr)) < 0) {
        perror("connect");
        return  1;
    }

    // ------------------------------------------
    // 子プロセスを作成して複数経路から受け取れるように
    // ------------------------------------------
    pid_t pid;
    pid = fork();
    if(pid < 0) {
        perror("fork");
        return 1;
    }
    if(pid == 0) {
        close(server_sock);
        while ((n = read(connect_sock, buf, BUF_LEN)) > 0) {
            ssize_t w = write(fd, buf, n);
            if (w < 0) {
                perror("write");
                _exit(1);
            }
        }
        close(connect_sock);
        _exit(0);
    }
    
    // ------------------------------------------
    // 親プロセスはconnect_sockを使わない
    // ------------------------------------------
    close(connect_sock);
    
    printf("Waiting for relay...\n");
    int sock = accept(server_sock, NULL, NULL);
    if (sock < 0) {
        perror("accept");
        return 1;
    }
    printf("Relay connected\n");

    while ((n = read(sock, buf, BUF_LEN)) > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) {
            perror("write");
            return 1;
        }
    }

    close(sock);
    printf("Relay disconnected\n");

    // ------------------------------------------
    // 子プロセス（直通の受信）が終わるのを待つ
    // ------------------------------------------
    waitpid(pid, NULL, 0);

    //時間計測終了
    clock_gettime(CLOCK_REALTIME, &end_time);
    sec = end_time.tv_sec - start_time.tv_sec;
    nsec = end_time.tv_nsec - start_time.tv_nsec;
    time = (double)sec + (double)nsec * 1e-9;

    close(fd);
    close(server_sock);


    //ファイルサイズ計測用コード
    long fsize;
    FILE *fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("filesize : %ld / time : %f\n", fsize, time);
    
    fclose(fp);

    return 0;
}