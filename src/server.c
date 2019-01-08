/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: alucas- <alucas-@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 1970/01/01 00:00:42 by alucas-           #+#    #+#             */
/*   Updated: 1970/01/01 00:00:42 by alucas-          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <ft/opts.h>
#include <ft/stdio.h>
#include <ft/stdlib.h>

#include <errno.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <wait.h>
#include <assert.h>

#define MAX_CMD_SZ  (1024)

struct client
{
	int fd;
	struct sockaddr_in addr;
};

typedef struct client clients_t[FT_P_LISTEN_QUEUE];

struct client *client_find(clients_t clients, int fd)
{
	unsigned i;

	for (i = 0; i < FT_P_LISTEN_QUEUE && clients[i].fd != fd; ++i);
	return i == FT_P_LISTEN_QUEUE ? NULL : clients + i;
}

int main(int ac, char *av[])
{
	if (ac != 2) {
		ft_fprintf(g_stderr, "Usage %s [port]\n Open a server\n",
		           av[0], ft_strerror(errno));
		return EXIT_FAILURE;
	}

	int const port = ft_atoi(av[1]);
	if (port <= 0 || port > 9999) {
		ft_fprintf(g_stderr, "%s: Invalid port argument\n", av[0]);
		return EXIT_FAILURE;
	}

	/* Open a network stream socket */
	int const sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) goto abort;

	/* Permit faster server reload */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))
		goto abort;

	struct sockaddr_in const saddr = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr.s_addr = INADDR_ANY
	};

	/* Set socket local address */
	if (bind(sock, (const struct sockaddr *)&saddr, sizeof saddr))
		goto abort;

	/* Listen for up to `FT_P_LISTEN_QUEUE` connections */
	if (listen(sock, FT_P_LISTEN_QUEUE))
		goto abort;

	fd_set rfds;

	clients_t clients = { };

	FD_ZERO(&rfds);

	FD_SET(STDIN_FILENO, &rfds);
	FD_SET(sock, &rfds);

	while (true) {
		char cmd[MAX_CMD_SZ];

		fd_set _rfds = rfds;

		int const err = select(FD_SETSIZE, &_rfds, NULL, NULL, NULL);
		if (err < 0) goto abort;

		if (FD_ISSET(sock, &_rfds)) {
			socklen_t sz = sizeof(struct sockaddr_in);
			struct client *cli = client_find(clients, 0);
			assert(cli);

			cli->fd = accept(sock, (struct sockaddr *)&cli->addr, &sz);
			FD_SET(cli->fd, &rfds);

			ft_printf("accepted new connection %s\n",
			          inet_ntoa(cli->addr.sin_addr));
		}

		if (FD_ISSET(STDIN_FILENO, &_rfds)) {
			ssize_t const rd = read(STDIN_FILENO, cmd, sizeof cmd);
			if (rd < 0) goto abort;

			cmd[rd] = '\0';

			if (ft_strcmp("quit\n", cmd) == 0) {
				ft_printf("quit !\n");
				goto success;
			}

			ft_printf("server: unknown command: %s", cmd);
		}

		for (struct client *cli = clients;
		     cli != clients + FT_P_LISTEN_QUEUE; ++cli) {

			if (!cli->fd || !FD_ISSET(cli->fd, &_rfds))
				continue;

			ssize_t const rd = recv(cli->fd, cmd, sizeof cmd, 0);
			if (rd < 0) {
				FD_CLR(cli->fd, &rfds);

				ft_fprintf(g_stderr, "connection error %s: %s\n",
				           cli->fd, inet_ntoa(cli->addr.sin_addr),
				           ft_strerror(errno));

				close(cli->fd);
				cli->fd = 0;
				continue;
			}

			cmd[rd] = '\0';

			if (ft_strcmp("quit\n", cmd) == 0) {
				FD_CLR(cli->fd, &rfds);

				ft_fprintf(g_stderr, "connection terminated %s\n",
				           inet_ntoa(cli->addr.sin_addr));

				close(cli->fd);
				cli->fd = 0;
				continue;
			}

			/* TODO: implement a fucking shell in a networking project.. */
			ft_printf("%s", cmd);
		}
	}

success:
	return EXIT_SUCCESS;

abort:
	ft_fprintf(g_stderr, "%s: %s\n", av[0], ft_strerror(errno));
	if (sock >= 0) close(sock);

	return EXIT_FAILURE;
}
