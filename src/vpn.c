/*
 * vpn.c
 *
 * Copyright (C) 2014 - 2015, Xiaoxiao <i@xiaoxiao.im>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "conf.h"
#include "crypto.h"
#include "log.h"
#include "tunif.h"
#include "utils.h"
#include "vpn.h"

static const conf_t *conf;

static volatile int running;
static int tun;
static int sock;
static struct
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
} remote;

static uint8_t buf[IV_LEN + MTU_MAX];

int vpn_init(const conf_t *config)
{
	char tun_name[32];

	conf = config;

	LOG("starting sipvpn %s", (conf->mode == server) ? "server" : "client");

	// set crypto key
	crypto_set_key(conf->key);

	// 初始化 UDP socket
	struct addrinfo hints;
	struct addrinfo *res;
	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (getaddrinfo(conf->server, conf->port, &hints, &res) != 0)
	{
		ERROR("getaddrinfo");
		return -1;
	}
	sock = socket(res->ai_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		ERROR("socket");
		freeaddrinfo(res);
		return -1;
	}
	setnonblock(sock);
	if (conf->mode == client)
	{
		// client
		memcpy(&remote.addr, res->ai_addr, res->ai_addrlen);
		remote.addrlen = res->ai_addrlen;
	}
	else
	{
		// server
		if (bind(sock, res->ai_addr, res->ai_addrlen) != 0)
		{
			ERROR("bind");
			close(sock);
			freeaddrinfo(res);
			return -1;
		}
	}
	freeaddrinfo(res);

	// 初始化 tun 设备
	strcpy(tun_name, conf->tunif);
	tun = tun_new(tun_name);
	if (tun < 0)
	{
		LOG("failed to init tun device");
		return -1;
	}
	if (strcmp(tun_name, conf->tunif) != 0)
	{
		setenv("tunif", tun_name, 1);
	}
	LOG("using tun device: %s", tun_name);

	// 执行 up hook
	if (conf->up[0] != '\0')
	{
		LOG("executing up hook: %s", conf->up);
		int r = shell(conf->up);
		if (r == 0)
		{
			LOG("done");
		}
		else
		{
			LOG("error: %d", r);
		}
	}

	// drop root privilege
	if (conf->user[0] != '\0')
	{
		if (runas(conf->user) != 0)
		{
			ERROR("runas");
		}
	}

	return 0;
}

int vpn_run(void)
{
	fd_set readset;
	ssize_t n;

	running = 1;
	while (running)
	{
		FD_ZERO(&readset);
		FD_SET(tun, &readset);
		FD_SET(sock, &readset);

		if (select((tun>sock?tun:sock) + 1, &readset, NULL, NULL, NULL) < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				ERROR("select");
				break;
			}
		}

		if (FD_ISSET(tun, &readset))
		{
			// 从 tun 设备读取 IP 包
			n = tun_read(tun, buf + IV_LEN, conf->mtu);
			if (n < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					// do nothing
				}
				else
				{
					ERROR("tun_read");
				}
			}
			else if (n == 0)
			{
				continue;
			}

			if (remote.addrlen != 0)
			{
				// 加密
				crypto_encrypt(buf, n);

				// 发送 UDP 包
				n = sendto(sock, buf, n + IV_LEN, 0,
				           (struct sockaddr *)&remote.addr, remote.addrlen);
				if (n <= 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
					{
						// do nothing
					}
					else
					{
						ERROR("sendto");
					}
				}
			}
		}

		if (FD_ISSET(sock, &readset))
		{
			// 读取 UDP 包
			remote.addrlen = sizeof(struct sockaddr_storage);
			n = recvfrom(sock, buf, conf->mtu + IV_LEN, 0,
			             (struct sockaddr *)&remote.addr, &remote.addrlen);
			if (n < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					// do nothing
				}
				else
				{
					ERROR("sendto");
				}
			}
			else if (n == 0)
			{
				continue;
			}

			// 解密
			if (crypto_decrypt(buf, n) != 0)
			{
				LOG("invalid packet, drop");
			}
			else
			{
				n = tun_write(tun, buf + IV_LEN, n - IV_LEN);
				if (n < 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
					{
						// do nothing
					}
					else
					{
						ERROR("tun_write");
					}
				}
			}
		}
	}

	// regain root privilege
	if (conf->user[0] != '\0')
	{
		if (runas("root") != 0)
		{
			ERROR("runas");
		}
	}

	// 执行 down hook
	if (conf->down[0] != '\0')
	{
		LOG("executing down hook: %s", conf->down);
		int r = shell(conf->down);
		if (r == 0)
		{
			LOG("done");
		}
		else
		{
			LOG("error: %d", r);
		}
	}

	LOG("exit");
	return (running == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

void vpn_stop(void)
{
	running = 0;
}
