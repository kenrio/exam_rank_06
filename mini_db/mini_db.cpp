#include <iostream>
#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fstream>

// 
volatile sig_atomic_t g_running = 1;

std::string	handle_command(const std::string &line, std::map<std::string, std::string> &db);
void		handle_signal(int sig);

int main(int argc, char **argv)
{
	if (argc != 3)
		return (1);

	int port = std::atoi(argv[1]);

	// クライアントごとのバッファ
	std::map<int, std::string> client_buffers;
	// key-value データベース
	std::map<std::string, std::string> db;

	// ファイルからの読み込み
	std::ifstream ifs(argv[2]);
	// ファイルが存在していることを確認（初回起動時はファイルがないので、この処理は飛ばされる）
	if (ifs.is_open())
	{
		std::string key, value;
		// >> 演算子はスペース・改行を区切りとして1単語ずつ読み取る
		while (ifs >> key >> value)
		{
			db[key] = value;
		}
		ifs.close();
	}

	// ソケット作成：電話機を買う
	// AF_INET: IPv4を使う（インターネットプロトコル）
	// SOCK_STREAM: TCP通信（データが順番通りに届くことを保証）
	// 0: プロトコルを自動選択（TCPの場合は自動でOK）
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	// server_fdはファイルディスクリプタでこの番号を使って、read/write/closeができる
	// 失敗する-1が返る
	if (server_fd < 0)
		return (1);

	// SO_REUSEADDR（再起動時にbindエラー防止）：購入した電話の再起動時の設定を行う
	// サーバーを止めてすぐ再起動するとOSが「ポートはまだ使用中」のエラーを出すことがあるため
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	//bind()でアドレスとポートを割り当て：設定項目を入れる「仕様書」用意
	struct sockaddr_in addr;
	// memsetで初期化: 構造体をゼロで埋める
	std::memset(&addr, 0, sizeof(addr));
	// sin_family: IPv4を再指定
	addr.sin_family = AF_INET;
	// 自分自身（localhost）のみで受け付ける（課題要件）
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	// ボート番号を設定、htons（Host To Network Short）はバイト順をネットワーク用に変換する関数
	addr.sin_port = htons(port);

	// 上記の設定をserver_fdに紐付ける：仕様書の内容を電話機に適用
	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (1);

	// listen()で接続を受け付ける状態にする
	// server_fd: どのソケットを受付状態にするか
	// 10: バックログ→同時に待たせておける接続要求の数
	if (listen(server_fd, 10) < 0)
		return (1);

	std::cout << "ready" << std::endl;

	// fd_setの準備: 監視したいfdの集合
	// master: 常に最新の監視リストを保持する
	// read_fds: select()に渡す作業用コピー
	fd_set master, read_fds;
	// リストを全部クリア
	FD_ZERO(&master);
	// サーバーのfdだけを監視対象に追加
	FD_SET(server_fd, &master);
	// select()にfdの最大値を伝えるための変数
	int fd_max = server_fd;
	
	// SIGINTを受けた時に呼ぶ関数を設定: signal_handler()
	signal(SIGINT, handle_signal);

	while (g_running)
	{
		// masterのfdリストを作業リストにコピー
		// ループから戻ると、read_fdsの中で「データが来ているfd」だけがセットされた状態になる
		read_fds = master;
		// select()でread_fdsのfdにイベントが起きるまでブロック
		if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) < 0)
		{
			if (!g_running)
				break ;
			continue ;	
		}

		// 全fdを順に見て、FD_ISSETで「このfdにイベントがあるか」をチェックする
		for (int fd = 0; fd <= fd_max; fd++)
		{
			if (!FD_ISSET(fd, &read_fds))
				continue;

			// server_fdにイベントがあるということは、新しいクライアント接続がきたということ
			if (fd == server_fd)
			{
				// 新しい接続を1つ受け入れて、そのクライアント専用の新しいfdを返す
				int client_fd = accept(server_fd, NULL, NULL);
				if (client_fd >= 0)
				{
					// 新しいfdをmasterに追加して、次のループから監視対象にする
					FD_SET(client_fd, &master);
					if (client_fd > fd_max)
						fd_max = client_fd;
				}
			}
			else // クライアントからデータ受信
			{
				char buf[1024];
				int n = recv(fd, buf, sizeof(buf) - 1, 0); // データを読む
				if (n <= 0) // データがなかったので切断処理
				{
					// 切断
					close(fd);
					FD_CLR(fd, &master);
					client_buffers.erase(fd);
				}
				else // データが届いたので、処理する
				{
					buf[n] = '\0';
					// 受信データをクライアント専用バッファに追加
					client_buffers[fd] += buf;

					// バファリング
					size_t pos;
					// \nを探す: 見つかる限り繰り返す
					while ((pos = client_buffers[fd].find('\n')) != std::string::npos)
					{
						// \nの 手前までを1コマンドとして切り出す
						std::string line = client_buffers[fd].substr(0, pos);

						// 処理済みの部分をバッファから削除（\nを含む）
						client_buffers[fd].erase(0, pos + 1);

						// 切り出したコマンドを処理して、応答を送る
						std::string response = handle_command(line, db);
						send(fd, response.c_str(), response.size(), 0);
					}
				}
			}
		}
	}

	// DBをファイルに保存
	std::ofstream ofs(argv[2]);
	for (std::map<std::string, std::string>::iterator it = db.begin(); it != db.end(); ++it)
	{
		ofs << it->first << " " << it->second << "\n";
	}
	ofs.close();

	// 全ソケットを閉じる
	for (int fd = 0; fd <= fd_max; fd++)
	{
		if (FD_ISSET(fd, &master))
			close(fd);
	}

	return (0);
}

std::string	handle_command(const std::string &line, std::map<std::string, std::string> &db)
{
	// 最初のスペースでコマンド名を取り出す
	size_t sp1 = line.find(' '); // 最初のスペースの位置を探す
	// std::string::nposはスペースが見つからなかった時の特別な値: DELETEや不明コマンドの処理に使える
	std::string cmd = (sp1 == std::string::npos) ? line : line.substr(0, sp1);

	if (cmd == "POST")
	{
		// "POST key value" → sp1の後にkeyとvalueがある
		size_t sp2 = line.find(' ', sp1 + 1);
		if (sp1 == std::string::npos || sp2 == std::string::npos)
			return ("2\n");
		// keyとvalueの切り出し: substr(開始位置, 文字数)
		std::string key = line.substr(sp1 + 1, sp2 - sp1 - 1);
		std::string value = line.substr(sp2 + 1);
		db[key] = value;
		return ("0\n");
	}
	else if (cmd == "GET")
	{
		std::string key = line.substr(sp1 + 1);
		if (db.find(key) != db.end())
			return ("0 " + db[key] + "\n");
		return ("1\n");
	}
	else if (cmd == "DELETE")
	{
		std::string key = line.substr(sp1 + 1);
		if (db.erase(key))
			return ("0\n");
		return ("1\n");
	}
	return ("2\n");
}

void	handle_signal(int sig)
{
	(void)sig;
	g_running = 0;
}
