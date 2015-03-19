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
#include "encrypt.h"
#include "log.h"
#include "tunif.h"
#include "utils.h"
#include "vpn.h"

static const conf_t *conf;

static volatile int running;
static int tun;
static char tun_name[32];
static int sock;
static struct
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
} remote;

static uint8_t udp_buf[MTU_MAX + OVERHEAD_LEN];
static uint8_t tun_buf[MTU_MAX];

int vpn_init(const conf_t *config)
{
	conf = config;

	// 初始化 tun 设备
	strcpy(tun_name, conf->tunif);
	tun = tun_new(tun_name);
	if (tun < 0)
	{
		LOG("failed to init tun device");
		return -1;
	}
	else if (conf->verbose)
	{
		LOG("using tun device: %s", tun_name);
	}

	// 执行 up hook
	if (conf->hook.up[0] != '\0')
	{
		LOG("executing %s", conf->hook.up);
		int r = shell(conf->hook.up);
		if (r != 0)
		{
			LOG("%s returned %d", conf->hook.up, r);
		}
	}

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
	if (conf->mode == client)
	{
		memcpy(&remote.addr, res->ai_addr, res->ai_addrlen);
		remote.addrlen = res->ai_addrlen;
	}
	else
	{
		if (bind(sock, res->ai_addr, res->ai_addrlen) != 0)
		{
			ERROR("bind");
			close(sock);
			freeaddrinfo(res);
			return -1;
		}
	}
	freeaddrinfo(res);

	setnonblock(sock);

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
			n = tun_read(tun, tun_buf, conf->mtu);
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
				encrypt(udp_buf, tun_buf, n, conf->key, conf->key_len);

				// 发送 UDP 包
				n = sendto(sock, udp_buf, n + OVERHEAD_LEN, 0,
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
				else
				{
					if (conf->verbose)
					{
						LOG("send %ld bytes to remote", n);
					}
				}
			}
		}

		if (FD_ISSET(sock, &readset))
		{
			// 读取 UDP 包
			n = recvfrom(sock, udp_buf, conf->mtu + OVERHEAD_LEN, 0,
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
			else if (conf->verbose)
			{
				LOG("recv %ld bytes from remote", n);
			}

			// 解密
			if (decrypt(tun_buf, udp_buf, n, conf->key, conf->key_len) != 0)
			{
				LOG("invalid packet, drop");
			}
			else
			{
				n = tun_write(tun, tun_buf, n - OVERHEAD_LEN);
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
				else if (conf->verbose)
				{
					LOG("write %ld bytes to %s", n, tun_name);
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
	if (conf->hook.down[0] != '\0')
	{
		LOG("executing %s", conf->hook.down);
		int r = shell(conf->hook.down);
		if (r != 0)
		{
			LOG("%s returned %d", conf->hook.down, r);
		}
	}

	if (running == 0)
	{
		LOG("exit gracefully");
		return EXIT_SUCCESS;
	}
	else
	{
		LOG("exit error");
		return EXIT_FAILURE;
	}
}

void vpn_stop(void)
{
	running = 0;
}
