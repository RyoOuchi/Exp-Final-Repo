/*  -*- coding: utf-8-unix; -*-                                     */
/*  FILENAME     :  tcp_file_server.c                                 */
/*  DESCRIPTION  :  Simple TCP file Server                          */
/*  USAGE:          tcp_file_server.out filename                    */
/*  VERSION      :                                                  */
/*  DATE         :  Sep 01, 2020                                    */
/*  UPDATE       :                                                  */
/*                                                                  */

/* 動き:
 *   1. 起動時に送るファイルの大きさを調べ、TCP_SERVER_PORTで接続を待つ。
 *   2. node1から直通（172.21.0.10）で接続が来たら、それを転送のきっかけにする。
 *      それ以外のアドレスからの接続は、何も送らずに閉じる。
 *      きっかけの接続でnode1から何かを受信することはない（要求は不要）。
 *   3. 経路の表（paths）の重みの比で、ファイルを経路ごとの連続した部分に分ける。
 *      最後の経路が、割り算の端数を含めてファイルの最後まで担当する。
 *   4. 経路ごとに子プロセスを作り、全経路で同時に送る。
 *        直通（dest が NULL）: 受け付けたnode1の接続に、そのまま担当分を送る。
 *        中継（dest がアドレス）: そのアドレス（中継の最初のホップ）に
 *                                自分から接続して、担当分を送る。
 *      各子は送り終えたら接続を閉じて終了する。中継はそれを受けて次のホップへの
 *      接続を順に閉じるので、node1に各経路の終わりが伝わる。
 *   5. 親はすぐ次のきっかけの接続を待つ。node1がもう一度接続すれば繰り返す。
 * 
 * 動かす前提（起動の順番）:
 *   node1の中継経路の待ち受け → node5の中継（次はnode1 172.23.0.10）
 *   → node2の中継（次はnode5 172.26.0.50）→ このサーバ
 *   → 最後にnode1がnode3（172.21.0.30）に直通で接続して転送開始。
 *   node1は直通の接続で何も送らないこと。
 */

#include "icslab2_net.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#define TRIGGER_PEER "172.21.0.10"

struct path {
    const char *dest;
    double weight;
};

// Optimizer shares for node 3 -> node 1, with a 100,000,000-byte file.
// Node 2 combines 61,491,293 + 1,634,119 bytes from its two onward routes.
// Its relay chooses once per connection; these weights do not enforce that split.
static const struct path paths[] = {
    { NULL,          0.01632592 }, // 3 -> 1 (existing direct connection)
    { "172.24.0.20", 0.63125412 }, // 3 -> 2 -> 5 -> 1, or 3 -> 2 -> 1
    { "172.27.0.40", 0.33609434 }, // 3 -> 4 -> 1
    { "172.28.0.50", 0.01632562 }, // 3 -> 5 -> 1
};
#define NUM_PATHS ((int)(sizeof(paths) / sizeof(paths[0])))

static void calc_range(int idx, off_t fileSize, off_t *offset, off_t *len)
{
    double total = 0.0, before = 0.0;
    off_t start, end;
    int i;

    for(i = 0; i < NUM_PATHS; i++) {
        total += paths[i].weight;
        if(i < idx) before += paths[i].weight;
    }
    start = (off_t)(fileSize * (before / total));
    if(idx == NUM_PATHS - 1) {
        end = fileSize;
    } else {
        end = (off_t)(fileSize * ((before + paths[idx].weight) / total));
    }
    *offset = start;
    *len = end - start;
}

static int connect_to(const char *ip)
{
    int s;
    struct sockaddr_in addr;

    if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_SERVER_PORT);
    if(inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid address: %s\n", ip);
        close(s);
        return -1;
    }
    if(connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(s);
        return -1;
    }
    return s;
}

static int write_all(int sock, const char *p, size_t len)
{
    while(len > 0) {
        ssize_t w = write(sock, p, len);
        if(w < 0) return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static void send_range(int sock, const char *filename, off_t offset, off_t len, int idx)
{
    char buf[BUF_LEN];
    int fd;
    off_t remaining = len, sent = 0;

    fd = open(filename, O_RDONLY);
    if(fd < 0) {
        perror("open");
        return;
    }
    if(lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return;
    }

    while(remaining > 0) {
        size_t want = (remaining < BUF_LEN) ? (size_t)remaining : BUF_LEN;
        ssize_t n = read(fd, buf, want);
        if(n <= 0) break;
        if(write_all(sock, buf, (size_t)n) < 0) {
            perror("write");
            break;
        }
        remaining -= n;
        sent += n;
    }

    printf("path %d: sent %lld / %lld bytes\n",
           idx, (long long)sent, (long long)len);
    close(fd);
}

int
main(int argc, char** argv)
{
    int     sock0;                  /* 待ち受け用ソケットディスクリプタ */
    int     sock;                   /* ソケットディスクリプタ */
    struct sockaddr_in  serverAddr; /* サーバ＝自分用アドレス構造体 */
    struct sockaddr_in  clientAddr; /* クライアント＝相手用アドレス構造体 */
    socklen_t addrLen;              /* clientAddrのサイズ */
    char   *filename;               /* 返送するファイルの名前 */
    int     yes = 1;                /* setsockopt()用 */
    struct stat st;
    off_t   fileSize;
    struct in_addr trigger;
    int     i;

    /* コマンドライン引数の処理 */
    if(argc != 2) {
        printf("Usage: %s filename\n", argv[0]);
        return 0;
    }
    /* 返送するファイル名 */
    filename = argv[1];

    if(stat(filename, &st) < 0) {
        perror("stat");
        return 1;
    }
    fileSize = st.st_size;
    printf("filename: %s, size: %lld bytes, paths: %d\n",
           filename, (long long)fileSize, NUM_PATHS);

    inet_pton(AF_INET, TRIGGER_PEER, &trigger);
    signal(SIGCHLD, SIG_IGN);

    /* STEP 1: TCPソケットをオープンする */
    if((sock0 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 1;
    }
    /* sock0のコネクションがTIME_WAIT状態でもbind()できるように設定 */
    setsockopt(sock0, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    /* STEP 2: クライアントからの要求を受け付けるIPアドレスとポートを設定する */
    memset(&serverAddr, 0, sizeof(serverAddr));     /* ゼロクリア */
    serverAddr.sin_family = AF_INET;                /* Internetプロトコル */
    serverAddr.sin_port = htons(TCP_SERVER_PORT);   /* 待ち受けるポート */
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* どのIPアドレス宛でも */

    /* STEP 3: ソケットとアドレスをbindする */
    if(bind(sock0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        return 1;
    }
    /* STEP 4: コネクションの最大同時受け入れ数を指定する */
    if(listen(sock0, 5) != 0) {
        perror("listen");
        return 1;
    }

    while(1) {
        /* STEP 5: クライアントからの接続要求を受け付ける */
        printf("waiting connection...\n");
        addrLen = sizeof(clientAddr);
        sock = accept(sock0, (struct sockaddr *)&clientAddr, &addrLen);
        if(sock < 0) {
            perror("accept");
            continue;
        }

        if(clientAddr.sin_addr.s_addr != trigger.s_addr) {
            printf("unknown peer: %s, closed\n", inet_ntoa(clientAddr.sin_addr));
            close(sock);
            continue;
        }
        printf("trigger from %s\n", inet_ntoa(clientAddr.sin_addr));

        for(i = 0; i < NUM_PATHS; i++) {
            off_t offset, len;
            pid_t pid;

            calc_range(i, fileSize, &offset, &len);
            printf("path %d (%s): offset %lld, length %lld\n",
                   i, paths[i].dest ? paths[i].dest : "direct",
                   (long long)offset, (long long)len);

            fflush(stdout);
            pid = fork();
            if(pid < 0) {
                perror("fork");
                continue;
            }

            if(pid == 0) {
                int out;

                close(sock0);

                if(paths[i].dest == NULL) {
                    out = sock;
                } else {
                    close(sock);
                    out = connect_to(paths[i].dest);
                    if(out < 0) {
                        _exit(1);
                    }
                }

                send_range(out, filename, offset, len, i);

                close(out);
                _exit(0);
            }
        }

        close(sock);
        printf("closed\n");
    }

    /* STEP 10: 待ち受け用ソケットのクローズ */
    close(sock0);
    return 0;
}

/* Local Variables: */
/* compile-command: "gcc tcp_file_server.c -o tcp_file_server.out" */
/* End: */
