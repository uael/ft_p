/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ftp.h                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: alucas- <alucas-@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 1970/01/01 00:00:42 by alucas-           #+#    #+#             */
/*   Updated: 1970/01/01 00:00:42 by alucas-          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ftp.h"
#include "netbuf.h"
#include "fsm.h"

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>

#define BUF_SIZE (4096)

struct netbuf
{
	uint8_t const *buf;
	uint16_t size;
};

static __always_inline void cli_close(struct ftp_srv *srv, struct ftp_cli *cli)
{
	if (errno)
		fprintf(stderr, "connection error %s: %s\n",
		        inet_ntoa(cli->addr.sin_addr), strerror(errno));

	FD_CLR(cli->socket, srv->rfds);
	close(cli->socket);
	cli->socket = 0;
}

static __always_inline struct ftp_cli *cli_find(ftp_srv_t *srv, int fd)
{
	unsigned i;
	for (i = 0; i < FTP_MAX_CLIENT && srv->clients[i].socket != fd; ++i);
	return (i == FTP_MAX_CLIENT) ? NULL : srv->clients + i;
}

enum {
	E_OPEN,
	E_RECV,
	E_TIMEOUT,

	C_USER,
	C_PASS,
};

enum {
	S_IDLE,
	S_WAIT_USER,
	S_WAIT_PASS,
	S_OPEN
};

#define SNS(s) (s),sizeof(s)-1

int on_open(fsm_t const *fsm, int ecode, void *arg)
{
	struct ftp_cli *const cli = container_of(fsm, struct ftp_cli, fsm);
	return (int)write(cli->socket, SNS("200 command successful.\r\n"));
}

static struct fsm_trans const *const stt[] = {
	[S_IDLE]      = (struct fsm_trans const[]){
		{ E_OPEN,        on_open, S_WAIT_USER },
		{ FSM_E_DEFAULT, NULL,    S_IDLE      },
	},

	[S_WAIT_USER] = (struct fsm_trans const[]){
		{ E_RECV,        NULL, S_WAIT_USER },
		{ C_USER,        NULL, S_WAIT_PASS },
		{ FSM_E_DEFAULT, NULL, S_WAIT_USER },
	},

	[S_WAIT_PASS] = (struct fsm_trans const[]){
		{ E_RECV,        NULL, S_WAIT_PASS },
		{ E_TIMEOUT,     NULL, S_WAIT_USER },
		{ C_PASS,        NULL, S_OPEN      },
		{ FSM_E_DEFAULT, NULL, S_WAIT_PASS },
	},

	[S_OPEN]      = (struct fsm_trans const[]){
		{ E_RECV,        NULL, S_OPEN      },
		{ FSM_E_DEFAULT, NULL, S_OPEN      },
	},
};

int ftp_srv_open(int port, char const *root,
                 fd_set *rfds, struct ftp_usr *users, ftp_srv_t *srv)
{
	if (port <= 1024 || port > 9999)
		return (errno = EINVAL), -1;

	/* Open a network stream socket */
	int const sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) goto abort;

	/* Faster server reload */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))
		goto abort;

	struct sockaddr_in const addr = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr.s_addr = INADDR_ANY
	};

	/* Set socket local address */
	if (bind(sock, (struct sockaddr const *)&addr, sizeof addr))
		goto abort;

	/* Listen for up to `FTP_MAX_CLIENT` connections */
	if (listen(sock, FTP_MAX_CLIENT))
		goto abort;

	/* Everything goes well, set rfds and save data to server structure */
	FD_SET(sock, rfds);
	return (*srv = (struct ftp_srv){
		.root = root,
		.rfds = rfds, .users = users,
		.socket = sock, .addr = addr,}), 0;

abort:
	if (sock >= 0) close(sock);
	return -1;
}

int ftp_srv_start(ftp_srv_t *srv, const fd_set *rfds, struct timeval *timeout)
{
	int err = 0;

	memset(timeout, 0, sizeof *timeout);

	if (gettimeofday(&srv->now, NULL)) return -1;

	if (FD_ISSET(srv->socket, rfds)) {
		socklen_t sz = sizeof(struct sockaddr_in);
		struct sockaddr_in addr;

		int const sock = accept(srv->socket, (struct sockaddr *)&addr, &sz);
		if (sock < 0) return -1;

		struct ftp_cli *const cli = cli_find(srv, 0);
		if (cli == NULL) {
			err = (int)write(sock, SNS("200 command successful.\r\n"));
			if (err < 0) return err;
		} else {
			cli->socket = sock;
			cli->addr = addr;
			FD_SET(cli->socket, srv->rfds);
			fsm_init(&cli->fsm, S_IDLE, stt);
			err = fsm_trigger(&cli->fsm, E_OPEN, NULL);
			if (err) return err;
		}
	}

	struct timeval *to = NULL;

	for (struct ftp_cli *cli = srv->clients;
	     cli != srv->clients + FTP_MAX_CLIENT; ++cli) {

		if (!cli->socket)
			continue;

		if (cli->timeout.tv_sec || cli->timeout.tv_usec) {
			if (timercmp(&cli->timeout, &srv->now, >=) == 0) {
				memset(&cli->timeout, 0, sizeof cli->timeout);
				err = fsm_trigger(&cli->fsm, E_TIMEOUT, NULL);
				if (err) break;
			} else if (to == NULL || timercmp(&cli->timeout, to, <) == 0)
				to = &cli->timeout;
		}

		if (!FD_ISSET(cli->socket, rfds))
			continue;

		char data[BUF_SIZE + 1];
		ssize_t const rd = recv(cli->socket, data, sizeof data, 0);
		if (rd < 0) {
			cli_close(srv, cli);
			continue;
		}

		struct netbuf buf = {
			.buf = (uint8_t const *)data,
			.size = (uint16_t)rd };

		data[rd] = '\0';
		err = fsm_trigger(&cli->fsm, E_RECV, &buf);
		if (err) break;
	}

	if (to) timersub(to, &srv->now, timeout);
	return err;
}
