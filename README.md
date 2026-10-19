# TCP通信の中継プログラム

TCP通信を使って、送信側から受信側へメッセージを中継するネットワーク実験用のCプログラムです。
`server` が送信する `Hello, World!` を、`relay` が `client` に転送し、`client` が画面に表示します。

```text
server（送信） → relay（中継） → client（受信・表示）
```

## ファイルの役割

| ファイル | 役割 |
| --- | --- |
| `server.c` | 指定した中継先に接続し、メッセージを1回送信して終了します。 |
| `relay.c` | 接続を待ち受け、受信したデータを指定した受信先へ転送します。 |
| `client.c` | 中継からの接続を待ち受け、受信したメッセージを表示します。 |
| `icslab2_net.h` | 共通のヘッダーです。TCPポート番号は `10000`、バッファ長は `1000` バイトです。 |

ファイル名は `server` が送信側、`client` が待ち受ける受信側になっています。

## コンパイル

Linux環境で、ソースコードのあるディレクトリから実行します。共通ヘッダーがLinux用の `sys/epoll.h` を読み込むため、macOSではそのままコンパイルできません。

```sh
gcc server.c -o server
gcc relay.c -o relay
gcc client.c -o client
```

## 実行方法

受信側 → 中継 → 送信側の順に起動します。以下のIPアドレスは、それぞれの実験環境のIPv4アドレスに置き換えてください。

1. 受信側で、出力ファイルを指定します。

   ```sh
   ./client out.txt
   ```

2. 中継側で、受信側のIPアドレスを指定します。

   ```sh
   ./relay 192.168.1.30
   ```

3. 送信側で、中継側のIPアドレスを指定します。

   ```sh
   ./server 192.168.1.20
   ```

正常に届くと、受信側には通常、次のような内容のファイルが指定された名前で生成されます。

```text
Hello, World!
```

TCPではデータが分割されて届くことがあるため、表示が複数行に分かれる場合もあります。
`client` と `relay` は通信終了後も次の接続を待ちます。終了するには `Ctrl+C` を押します。

`client` と `relay` はどちらもポート `10000` で待ち受けるため、別々のマシンや仮想マシンで起動してください。同じネットワーク名前空間ではポートが競合します。

## 経路選択とデータ分配の計算

`best_route/route_optimizer.py` で、`Link` オブジェクトにまとめたビットレートとRTTから、使用経路とファイルの分配量を計算できます。測定値は同ファイルの `NETWORK_LINKS` に設定してあり、CSVの読み込みは不要です。

初回の準備（このプロジェクトでは実施済み）:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r best_route/requirements.txt
```

実行:

```sh
.venv/bin/python best_route/route_optimizer.py
```

送信元・宛先・ファイルサイズはPythonファイル先頭の `SOURCE`、`DESTINATION`、`FILE_SIZE_BYTES` を編集します。
測定値は `Link(送信元, 宛先, ビットレート[Mbit/s], RTT[ms])` で設定します。入力値が正しいことを前提にした簡単な実装です。

DFSで候補経路を探し、共有リンクの容量を守る線形計画と二分探索で完了時間を求めます。
結果の `Data` は経路ごとのbytes、`Share` はファイル全体に対する割合です。
表示時間は一定の速度と1 RTTの遅延を仮定した推定値です。実際のファイル送信は行いません。

## データの生成方法
```
gcc create_file.c -o create_file.out
create_file.out 10
```
これで10MBのファイル（test.bin）を作れるはず