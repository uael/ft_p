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

#define MAX_CMD_SZ  (1024)

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

	FD_ZERO(&rfds);

	FD_SET(STDIN_FILENO, &rfds);
	FD_SET(sock, &rfds);

	while (true) {
		char cmd[MAX_CMD_SZ];

		fd_set _rfds = rfds;

		int const err = select(FD_SETSIZE, &_rfds, NULL, NULL, NULL);
		if (err < 0) goto abort;

		for (int fd = 0; fd < FD_SETSIZE; ++fd) {
			if (!FD_ISSET(fd, &_rfds)) continue;

			if (fd == sock) {
				struct sockaddr_in addr;
				socklen_t sz = sizeof(struct sockaddr_in);

				int const client = accept(sock, (struct sockaddr *) &addr, &sz);
				FD_SET(client, &rfds);

				ft_printf("accepted new connection %s\n",
				          inet_ntoa(addr.sin_addr));
			}
			else if (fd == STDIN_FILENO) {
				ssize_t const rd = read(fd, cmd, sizeof cmd);
				if (rd < 0) goto abort;

				if (ft_strncmp("q", cmd, 1) == 0) {
					ft_printf("quit !\n");
					goto success;
				}

				ft_printf("%.*s\n", (unsigned)rd, cmd);
			}
			else {
				ssize_t const rd = recv(fd, cmd, sizeof cmd, 0);
				if (rd < 0) {
					FD_CLR(fd, &rfds);

					ft_fprintf(g_stderr, "client error %d: %s\n",
					           fd, ft_strerror(errno));
					close(fd);
					continue;
				}

				ft_printf("%.*s", (unsigned)rd, cmd);
			}
		}
	}

success:
	return EXIT_SUCCESS;

abort:
	ft_fprintf(g_stderr, "%s: %s\n", av[0], ft_strerror(errno));
	if (sock >= 0) close(sock);

	return EXIT_FAILURE;
}
