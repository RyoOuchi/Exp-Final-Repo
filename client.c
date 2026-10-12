#include "icslab2_net.h"

int main(int argc, char *argv[]) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

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

    close(server_sock);

    return 0;
}