#include <stdint.h>
#include "icslab2_net.h"

void send_to_relay(const char *ip_address, const char *message);

int main(int argc, char *argv[]) {
    char* message = "Hello, World!";
    char* relay_ip = argv[1];

    send_to_relay(relay_ip, message);

    return 0;
}


void send_to_relay(const char *ip_address, const char *message) {
    int sock;
    struct sockaddr_in relayAddr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&relayAddr, 0, sizeof(relayAddr));
    relayAddr.sin_family = AF_INET;
    relayAddr.sin_port = htons(TCP_SERVER_PORT);

    inet_pton(AF_INET, ip_address, &relayAddr.sin_addr);

    connect(sock, (struct sockaddr *)&relayAddr, sizeof(relayAddr));

    size_t length = strlen(message);
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = write(sock, message + sent, length - sent);
        if (n <= 0) {
            perror("write");
            break;
        }

        sent += n;
    }

    close(sock);
}