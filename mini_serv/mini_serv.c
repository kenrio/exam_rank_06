#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

void	send_to_all(int exclude_fd, int server_fd, fd_set *master, int fd_max, char *msg);

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}

	// fdからクライアント情報を引くための配列
	// fdの最大値は通常1024未満なので、固定サイズで十分
	int		client_id[1024]; // client_fd[fd] = そのクライアントのID
	char	*client_buf[1024]; // client_buf[fd] = そのクライアントの受信バッファ
	int		next_id = 0; // 次に割り当てるID

	int port = atoi(argv[1]);

	// ソケット作成
	// AF_INET: IPv4を使う（インターネットプロトコル）
	// SOCK_STREAM: TCP通信（データが順番通りに届くことを保証）
	// 0: プロトコルを自動選択（TCPの場合は自動でOK）
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		write(2, "Fatal error\n", 12);
		exit(1);
	}

	// bind() でアドレスとポートを割り当て
	struct sockaddr_in addr;
	// memset() で初期化：構造体をゼロで埋める
	memset(&addr, 0, sizeof(addr));
	// sin_family: IPv4を再指定
	addr.sin_family = AF_INET;
	// INADDR_LOOPBACKは 127.0.0.1 を表す定数
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
	// ポート番号を設定、htons (Host To Network Short) バイト順をネットワーク用に変換する関数
	addr.sin_port = htons(port);

	// 上記の設定をserver_fdに紐づける
	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		write(2, "Fatal error\n", 12);
		exit(1);
	}

	// listen() で接続を受け付ける状態にする
	// server_fd: どのソケットを受付状態にするか
	// 10: バックログ→同時に待たせておける接続要求の数
	if (listen(server_fd, 10) < 0)
	{
		write(2, "Fatal error\n", 12);
		exit(1);
	}

	// selectループ
	// master: 常に最新の監視リストを保持する
	// read_fds: select() に渡す作業用コピー
	fd_set master, read_fds;
	// リストを全部クリア
	FD_ZERO(&master);
	// サーバーのfdだけを監視対象に追加
	FD_SET(server_fd, &master);
	// select() にfdの最大値を伝えるための変数
	int fd_max = server_fd;

	while (1)
	{
		// masterのfdリストを作業リストにコピー
		// ループから戻ると、read_fdsの中で「データが来ているfd」だけがセットされた状態になる
		read_fds = master;
		// select() でread_fdsのfdにイベントが起きるまでブロック
		if (select(fd_max + 1, & read_fds, NULL, NULL, NULL) < 0)
			continue ;

		// 全fdを順に見て、FD_ISSETで「このfdにイベントがあるか」をチェックする
		for (int fd = 0; fd <= fd_max; fd++)
		{
			if (!FD_ISSET(fd, &read_fds))
				continue ;

			// server_fdにイベントがあるということは、新しいクライアント接続がきたということ
			if (fd == server_fd)
			{
				// 新しいクライアント接続: クライアント専用の新しいfdを返す
				int client_fd = accept(server_fd, NULL, NULL);
				if (client_fd >= 0)
				{
					// 新しいfdをmasterに追加して、次のループから監視対象にする
					FD_SET(client_fd, &master);
					if (client_fd > fd_max)
						fd_max = client_fd;

					// ID割り当て: 0から連番（課題要件）
					client_id[client_fd] = next_id++;
					// クライアント専用のバッファを初期化
					client_buf[client_fd] = NULL;

					// 接続メッセージ
					char	msg[64];
					snprintf(msg, sizeof(msg), "server: client %d just arrived\n", client_id[client_fd]);
					send_to_all(client_fd, server_fd, &master, fd_max, msg);
				}
			}
			else // クライアントからデータ受信
			{
				char buf[4096];
				int n = recv(fd, buf, sizeof(buf) - 1, 0); // データを読む
				if (n <= 0) // データがなかったので切断処理
				{
					// 切断メッセージ
					char	msg[64];
					snprintf(msg, sizeof(msg), "server: client %d just left\n", client_id[fd]);
					send_to_all(fd, server_fd, &master, fd_max, msg);

					// クリーンアップ
					close(fd);
					FD_CLR(fd, &master);
					free(client_buf[fd]);
					client_buf[fd] = NULL;
				}
				else // データが届いたので、処理する
				{
					buf[n] = '\0';
					// TODO: メッセージ転送
					if (client_buf[fd] == NULL)
					{
						// recv() で受信したバイト + 1を確保して0初期化
						client_buf[fd] = calloc(n + 1, 1);
						if (!client_buf[fd])
						{
							write(2, "Fatal error\n", 12);
							exit(1);
						}
						// bufの内容をクライアント専用バッファにコピー
						strcpy(client_buf[fd], buf);
					}
					else
					{
						// バッファを既存バッファ + 新たに受信したバイト + 1分拡張
						char	*new_buf = realloc(client_buf[fd], strlen(client_buf[fd] + n + 1));
						if (!new_buf)
						{
							write(2, "Fatal error\n", 12);
							exit(1);
						}
						// 拡張したバッファをクライアント専用fdにコピー
						client_buf[fd] = new_buf;
						strcat(client_buf[fd], buf);
					}

					char	*pos;
					// クライアント専用バッファから最初の改行を検索して、そのポインタ（位置）を取得
					while ((pos = strstr(client_buf[fd], "\n")) != NULL)
					{
						// 取得した改行位置を'\0'に置き換える
						*pos = '\0';

						char	prefix[32];
						sprintf(prefix, "client %d: ", client_id[fd]);

						int		msg_len = strlen(prefix) + strlen(client_buf[fd]) + 1;
						char	*msg = malloc(msg_len + 1);
						if (!msg)
						{
							write(2, "Fatal erro\n", 12);
							exit(1);
						}
						strcpy(msg, prefix);
						strcat(msg, client_buf[fd]);
						strcat(msg, "\n");

						send_to_all(fd, server_fd, &master, fd_max, msg);
						free(msg);

						char	*remaining = malloc(strlen(pos + 1) + 1);
						if (!remaining)
						{
							write(2, "Fatal error\n", 12);
							exit(1);
						}
						strcpy(remaining, pos + 1);
						free(client_buf[fd]);
						client_buf[fd] = remaining;
					}
				}
			}
		}
	}

	return (0);
}

void	send_to_all(int exclude_fd, int server_fd, fd_set *master, int fd_max, char *msg)
{
	for (int fd = 0; fd <= fd_max; fd++)
	{
		if (fd != exclude_fd && fd != server_fd && FD_ISSET(fd, master))
		{
			send(fd, msg, strlen(msg), 0);
		}
	}
	return ;
}
