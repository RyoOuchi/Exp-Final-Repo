#include "icslab2_net.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

typedef struct
{
    int incoming_sock;
    char next_ip[INET_ADDRSTRLEN];
} RelayArgs;

int connect_to_ip(const char *ip_address)
{
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

void *relay_worker(void *arg)
{
    RelayArgs args = *(RelayArgs *)arg;
    free(arg);

    int incoming_sock = args.incoming_sock;
    int outgoing_sock = connect_to_ip(args.next_ip);

    char buf[BUF_LEN];

    printf("Forwarding connection to %s\n", args.next_ip);

    while (1)
    {
        ssize_t n = read(incoming_sock, buf, sizeof(buf));
        if (n == 0)
        {
            break;
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("read");
            break;
        }

        size_t sent = 0;

        while (sent < (size_t)n)
        {
            ssize_t written = send(
                outgoing_sock,
                buf + sent,
                (size_t)n - sent,
                MSG_NOSIGNAL);

            if (written < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("send");
                goto cleanup;
            }

            sent += (size_t)written;
        }
    }
cleanup:
    close(outgoing_sock);
    close(incoming_sock);
    printf("Forwarding connection finished\n");
    return NULL;
}

const char *get_next_ip(const char *current_ip, const char *sender_ip)
{
    // Node 4: node 3 → node 1
    if (strcmp(current_ip, "172.27.0.40") == 0 &&
        strcmp(sender_ip, "172.27.0.30") == 0)
    {
        return "172.22.0.10";
    }

    // Node 5: node 3 → node 1
    if (strcmp(current_ip, "172.28.0.50") == 0 &&
        strcmp(sender_ip, "172.28.0.30") == 0)
    {
        return "172.23.0.10";
    }

    // Node 5: node 2 → node 1
    if (strcmp(current_ip, "172.26.0.50") == 0 &&
        strcmp(sender_ip, "172.26.0.20") == 0)
    {
        return "172.23.0.10";
    }

    // Node 2: node 3 → either node 5 or node 1
    if (strcmp(current_ip, "172.24.0.20") == 0 &&
        strcmp(sender_ip, "172.24.0.30") == 0)
    {
        static pthread_mutex_t random_mutex =
            PTHREAD_MUTEX_INITIALIZER;

        const double probability_to_node5 =
            61491293.0 / (61491293.0 + 1634119.0);

        pthread_mutex_lock(&random_mutex);

        double r = (double)rand() / ((double)RAND_MAX + 1.0);

        pthread_mutex_unlock(&random_mutex);

        if (r < probability_to_node5)
        {
            return "172.26.0.50"; // Node 5: about 97.4113%
        }

        return "172.20.0.10"; // Node 1: about 2.5887%
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));
    if (argc < 2)
    {
        printf("Usage: %s <destination_ip>\n", argv[0]);
        return 1;
    }


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

    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

    while (1)
    {
        printf("Waiting for connection...\n");

        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);

        int sock = accept(server_sock, (struct sockaddr *)&peer, &peer_len);

        if (sock < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("accept");
            continue;
        }

        // センダーのIPアドレス
        char sender_ip[INET_ADDRSTRLEN];
        strcpy(sender_ip, inet_ntoa(peer.sin_addr));
        printf("Client connected from %s\n", sender_ip);

        // 自分のIPアドレスを取得
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        if (getsockname(sock, (struct sockaddr *)&local, &local_len) == -1)
        {
            perror("getsockname");
            close(sock);
            continue;
        }
        char local_ip[INET_ADDRSTRLEN];
        strcpy(local_ip, inet_ntoa(local.sin_addr));
        printf("Local IP address: %s\n", local_ip);

        const char *next_ip = get_next_ip(local_ip, sender_ip);

        if (next_ip == NULL)
        {
            fprintf(stderr, "No matching route\n");
            close(sock);
            continue;
        }

        RelayArgs *args = malloc(sizeof(*args));

        if (args == NULL)
        {
            perror("malloc");
            close(sock);
            continue;
        }

        args->incoming_sock = sock;
        strcpy(args->next_ip, next_ip);

        pthread_t thread;

        int error = pthread_create(&thread, &thread_attr, relay_worker, args);

        if (error != 0)
        {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            free(args);
            close(sock);
            continue;
        }
    }
    close(server_sock);
    return 0;
}
