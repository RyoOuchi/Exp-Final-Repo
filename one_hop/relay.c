#include "icslab2_net.h"

int connect_to_ip(const char *ip_address) {
    int sock;
    struct sockaddr_in relayAddr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&relayAddr, 0, sizeof(relayAddr));
    relayAddr.sin_family = AF_INET;
    relayAddr.sin_port = htons(TCP_SERVER_PORT);

    inet_pton(AF_INET, ip_address, &relayAddr.sin_addr);

    connect(sock, (struct sockaddr *)&relayAddr, sizeof(relayAddr));

    return sock;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <destination_ip>\n", argv[0]);
        return 1;
    }

    char *client_ip = argv[1];

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
        printf("Waiting for connection...\n");
        int sock = accept(server_sock, NULL, NULL);
        printf("Client connected\n");
        int outgoing_sock = connect_to_ip(client_ip);
        while ((n = read(sock, buf, BUF_LEN)) > 0) {
            int sent = 0;
            while (sent < n) {
                ssize_t written = write(
                    outgoing_sock,
                    buf + sent,
                    n - sent
                );
                if (written <= 0) {
                    perror("write");
                    break;
                }
                sent += written;
            }
            printf("Forwarded %d bytes\n", sent);
            if (sent < n) {
                break;
            }
        }
        close(outgoing_sock);
        close(sock);
        printf("Client disconnected\n");
    }
    close(server_sock);
    return 0;
}