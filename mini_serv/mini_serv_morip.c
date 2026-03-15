#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

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

typedef struct s_client
{
	int id;
	char *msg;
} t_client;

t_client clients[1024];
int max_fd = 0;
int next_fd = 0;
fd_set active_fds, write_fds, read_fds;
char buf[4096 * 42];
char send_buf[4096 * 42 + 100];

void fatal_error()
{
	write(2,"Fatal error\n", 12);
	exit(1);
}

void broadcast(int sender_fd, char *msg)
{
	for (int fd = 0; fd <= max_fd; fd++)
	{
		if (FD_ISSET(fd, &write_fds) && fd != sender_fd)
			send(fd, msg, strlen(msg), 0);
	}
}

int main(int ac, char *av[]) {
	if (ac != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
		fatal_error();
	max_fd = server_fd;
	FD_ZERO(&active_fds);
	FD_SET(server_fd, &active_fds);

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //127.0.0.1
	servaddr.sin_port = htons(atoi(av[1]));

	if (bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		fatal_error();
	if (listen(server_fd, 128) != 0)
		fatal_error();

	while (1)
	{
		write_fds = read_fds = active_fds;
		if (select(max_fd + 1, &read_fds, &write_fds, NULL, NULL) < 0)
			continue;

		for (int fd = 0; fd <= max_fd; fd++)
		{
			if (FD_ISSET(fd, &read_fds))
			{
				if (fd == server_fd)
				{
					struct sockaddr_in cliaddr;
					socklen_t len = sizeof(cliaddr);
					int client_fd = accept(fd, (struct sockaddr *)&cliaddr, &len);
					if (client_fd < 0)
						continue;
					if (client_fd > max_fd)
						max_fd = client_fd;
					clients[client_fd].id = next_fd++;
					clients[client_fd].msg = NULL;
					FD_SET(client_fd, &active_fds);
					sprintf(send_buf, "server: client %d just arrived\n", clients[client_fd].id);
					broadcast(client_fd, send_buf);
				}
				else
				{
					int n = recv(fd, buf, 4096 * 40, 0);
					if (n <= 0)
					{
						sprintf(send_buf, "server: client %d just left\n", clients[fd].id);
						broadcast(fd, send_buf);
						free(clients[fd].msg);
						clients[fd].msg = NULL;
						FD_CLR(fd, &active_fds);
						close(fd);
					}
					else
					{
						buf[n] = '\0';
						clients[fd].msg = str_join(clients[fd].msg, buf);
						if (clients[fd].msg == 0)
							fatal_error();
						char *msg_to_send;
						int ret;
						while ((ret = extract_message(&clients[fd].msg, &msg_to_send)) != 0)
						{
							if (ret < 0)
								fatal_error();
							sprintf(send_buf, "client %d: %s", clients[fd].id, msg_to_send);
							broadcast(fd, send_buf);
							free(msg_to_send);
						}
					}
				}
			}
		}
	}
	return (0);
}