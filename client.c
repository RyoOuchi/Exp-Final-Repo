#include "icslab2_net.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char *argv[]) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    /*時間計測用*/
    unsigned int sec;
    int nsec;
    double time;
    unsigned int w_usec;
    struct timespec start_time, end_time;

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

    //時間計測開始
    clock_gettime(CLOCK_REALTIME, &start_time);

    while (1) {
        printf("Waiting for relay...\n");
        int sock = accept(server_sock, NULL, NULL);
        printf("Relay connected\n");

        while ((n = read(sock, buf, BUF_LEN - 1)) > 0) {
            buf[n] = '\0';

            printf("Received: %s\n", buf);
        }

        close(sock);
        printf("Relay disconnected\n");
    }

    //時間計測終了
    clock_gettime(CLOCK_REALTIME, &end_time);
    sec = end_time.tv_sec - start_time.tv_sec;
    nsec = end_time.tv_nsec - start_time.tv_nsec;
    time = (double)sec + (double)nsec * 1e-9;
    printf("Total time : %f\n",time);

    close(server_sock);


    /*ファイルサイズ計測用コード
    long fsize;
    FILE *fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("filesize : %ld / time : %f\n", fsize, time);
    
    fclose(fp);
    */
    return 0;
}