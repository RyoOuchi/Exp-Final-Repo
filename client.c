#include "icslab2_net.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char *argv[]) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

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
        fd = open(filename, O_CREAT | O_WRONLY, 0644);
        if(fd < 0) {
            perror("open");
            return 1;
        }
    }else{
        printf("Usage: %s [o_filename]\n", argv[0]);
        return 0;
    }

    struct timespec start_time, end_time;
    long total = 0; // bytes received
    int started = 0;

    printf("Waiting for relay...\n");
    int sock = accept(server_sock, NULL, NULL);
    printf("Relay connected\n");

    while ((n = read(sock, buf, BUF_LEN)) > 0) {
        if (!started) {
            clock_gettime(CLOCK_MONOTONIC, &start_time); // start at first data
            started = 1;
        }
        write(fd, buf, n);
        total += n;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time); // relay closed = done
    printf("Relay disconnected\n");

    double elapsed = (end_time.tv_sec  - start_time.tv_sec)
                   + (end_time.tv_nsec - start_time.tv_nsec) * 1e-9;

    close(sock);
    close(fd);
    close(server_sock);

    printf("filesize : %ld / time : %f\n", total, elapsed);
    return 0;
}