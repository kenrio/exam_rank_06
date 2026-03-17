#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

// fdからクライアント情報を引くための配列
// fdの最大値は通常1024未満なので、固定サイズで十分
int		client_id[1024]; // client_fd[fd] = そのクライアントのID
char	*client_buf[1024]; // client_buf[fd] = そのクライアントの受信バッファ
int		next_id = 0; // 次に割り当てるID

int		server_fd;
int		fd_max;
fd_set	master, write_fds;

void	fatal_error(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

void	broadcast(int exclude_fd, char *msg)
{
	for (int fd = 0; fd <= fd_max; ++fd)
	{
		if (fd != exclude_fd && fd != server_fd && FD_ISSET(fd, &write_fds))
			send(fd, msg, strlen(msg), 0);
	}

	return ;
}

void	handle_new_connection(void)
{
	int	client_fd = accept(server_fd, NULL, NULL);
	if (client_fd < 0)
		return ;

	FD_SET(client_fd, &master);
	if (client_fd > fd_max)
		fd_max = client_fd;

	client_id[client_fd] = next_id++;
	client_buf[client_fd] = NULL;

	char	msg[64];
	sprintf(msg, "server: client %d just arrived\n", client_id[client_fd]);
	broadcast(client_fd, msg);

	return ;
}

void	handle_disconnect(int fd)
{
	char	msg[64];
	sprintf(msg, "server: client %d just left\n", client_id[fd]);
	broadcast(fd, msg);

	close(fd);
	FD_CLR(fd, &master);
	free(client_buf[fd]);
	client_buf[fd] = NULL;

	return ;
}

void	process_buf(int fd)
{
	char	*msg;
	int		ret;

	while ((ret = extract_message(&client_buf[fd], &msg)) == 1)
	{
		char	prefix[32];
		sprintf(prefix, "client %d: ", client_id[fd]);

		char	*full = str_join(NULL, prefix);
		if (!full)
			fatal_error();
		full = str_join(full, msg);
		if (!full)
			fatal_error();

		broadcast(fd, full);
		free(full);
		free(msg);
	}
	if (ret == -1)
		fatal_error();
}

void	handle_message(int fd)
{
	char	buf[4096];
	int		n = recv(fd, buf, sizeof(buf) - 1, 0);

	if (n <= 0)
	{
		handle_disconnect(fd);
		return ;
	}

	buf[n] = '\0';

	client_buf[fd] = str_join(client_buf[fd], buf);
	if (!client_buf[fd])
		fatal_error();

	process_buf(fd);
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}

	int port = atoi(argv[1]);

	// ソケット作成
	// AF_INET: IPv4を使う（インターネットプロトコル）
	// SOCK_STREAM: TCP通信（データが順番通りに届くことを保証）
	// 0: プロトコルを自動選択（TCPの場合は自動でOK）
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
		fatal_error();

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
		fatal_error();

	// listen() で接続を受け付ける状態にする
	// server_fd: どのソケットを受付状態にするか
	// 10: バックログ→同時に待たせておける接続要求の数
	if (listen(server_fd, 10) < 0)
		fatal_error();

	// master: 常に最新の監視リストを保持する
	// read_fds: select() に渡す作業用コピー
	// リストを全部クリア
	FD_ZERO(&master);
	// サーバーのfdだけを監視対象に追加
	FD_SET(server_fd, &master);
	// select() にfdの最大値を伝えるための変数
	fd_max = server_fd;

	while (1)
	{
		// masterのfdリストを作業リストにコピー
		// ループから戻ると、read_fdsの中で「データが来ているfd」だけがセットされた状態になる
		fd_set read_fds = master;
		write_fds = master;
		// select() でread_fdsのfdにイベントが起きるまでブロック
		if (select(fd_max + 1, & read_fds, &write_fds, NULL, NULL) < 0)
			continue ;

		// 全fdを順に見て、FD_ISSETで「このfdにイベントがあるか」をチェックする
		for (int fd = 0; fd <= fd_max; fd++)
		{
			if (!FD_ISSET(fd, &read_fds))
				continue ;

			// server_fdにイベントがあるということは、新しいクライアント接続がきたということ
			if (fd == server_fd)
				handle_new_connection();
			else // クライアントからデータ受信
				handle_message(fd);
		}
	}

	return (0);
}
