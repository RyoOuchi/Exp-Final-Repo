#include "icslab2_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>

void recv_loop(int sock, int fd) {//for子
    char buf[BUF_LEN];
    int n;
    while ((n = read(sock, buf, BUF_LEN)) > 0) {
        write(fd, buf, n);
    }
    close(sock);
    _exit(0);
}

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

    int n_relay = 0;
    int n_child = 0;

    int yes = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
               &yes, sizeof(yes));

    struct sockaddr_in serverAddr = {0};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(TCP_SERVER_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(server_sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(server_sock, 5);

    //ファイルを開く
    if(argc == 3){
        filename = argv[1];
        fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fd < 0) {
            perror("open");
            return 1;
        }
        n_relay = (int)strtol(argv[2], NULL, 10);
    }else{
        printf("Usage: %s [o_filename] [n_relay]\n", argv[0]);
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

    if(fork() == 0) {
        close(server_sock);
        recv_loop(connect_sock, fd);
    }

    close(connect_sock);
    n_child++;

    printf("Waiting for %d relay(s) \n", n_relay);
    for(int i = 0; i < n_relay; i++){
        int sock = accept(server_sock, NULL, NULL);
        printf("Relay %d connected\n", i);

        if(fork() == 0){
            close(server_sock);
            recv_loop(sock, fd);
        }
        close(sock);
        n_child++;
    }

    for(int i = 0; i < n_child; i++){ //全子プロセスが終わるのをここでまつ
        wait(NULL);
    }

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
