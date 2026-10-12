#include <stdint.h>
#include "icslab2_net.h"

void send_to_relay(const char *ip_address, const char* file_path);

int main(int argc, char *argv[]) {
    char* message = "Hello, World!";
    char* file_path = argv[2];
    char* relay_ip = argv[1];

    send_to_relay(relay_ip, file_path);

    return 0;
}

void send_to_relay(const char *ip_address, const char* file_path) {
    int sock;
    struct sockaddr_in relayAddr;

    int fd;
    fd = open(file_path, O_RDONLY);

    if (fd < 0) {
        perror("open");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&relayAddr, 0, sizeof(relayAddr));
    relayAddr.sin_family = AF_INET;
    relayAddr.sin_port = htons(TCP_SERVER_PORT);

    inet_pton(AF_INET, ip_address, &relayAddr.sin_addr);

    connect(sock, (struct sockaddr *)&relayAddr, sizeof(relayAddr));

    char buffer[BUF_LEN];
    ssize_t n;

    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        write(sock, buffer, n);
    }

    close(fd);
    close(sock);
}