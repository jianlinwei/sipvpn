/*
 * conf.c - parse config file
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conf.h"
#include "md5.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#else
#  define PACKAGE "sipvpn"
#  define PACKAGE_BUGREPORT "https://github.com/XiaoxiaoPu/sipvpn/issues"
#  define VERSION "0.1.0"
#endif

#define LINE_MAX 1024

static void help(void)
{
	printf("usage: %s\n"
	       "  -h, --help           show this help\n"
	       "  -c, --config <file>  config file\n"
	       "  -d, --daemon         daemonize after initialization\n"
	       "  --pidfile <file>     PID file\n"
	       "  --logfile <file>     log file\n"
	       "  -V, --version        print version and exit\n\n"
	       "Bug report: <%s>.\n", PACKAGE, PACKAGE_BUGREPORT);
}

static void _strncpy(char *dest, const char *src, size_t n)
{
	char *end = dest + n;
	while ((dest < end) && ((*dest = *src) != '\0'))
	{
		dest++;
		src++;
	}
	*(end - 1) = '\0';
}

int read_conf(const char *file, conf_t *conf)
{
	FILE *f = fopen(file, "rb");
	if (f == NULL)
	{
		fprintf(stderr, "failed to open config file\n");
		return -1;
	}

	int line_num = 0;
	char buf[LINE_MAX];

	while (!feof(f))
	{
		char *line = fgets(buf, LINE_MAX, f);
		if (line == NULL)
		{
			break;
		}
		line_num++;
		// 跳过行首空白符
		while (isspace(*line))
		{
			line++;
		}
		// 去除行尾的空白符
		char *end = line + strlen(line) - 1;
		while ((end >= line) && (isspace(*end)))
		{
			*end = '\0';
			end--;
		}
		// 跳过注释和空白行
		if ((*line == '#') || (*line == '\0'))
		{
			continue;
		}
		// 开始解析
		char *p = strchr(line, '=');
		if (p == NULL)
		{
			fprintf(stderr, "parse conf file failed at line: %d\n", line_num);
			fclose(f);
			return -1;
		}
		*p = '\0';
		char *key = line;
		char *value = p + 1;
		if (strcmp(key, "password") != 0)
		{
			setenv(key, value, 1);
		}
		if (strcmp(key, "user") == 0)
		{
			_strncpy(conf->user, value, sizeof(conf->user));
		}
		else if (strcmp(key, "mode") == 0)
		{
			if (strcmp(value, "server") == 0)
			{
				conf->mode = server;
			}
			else if (strcmp(value, "client") == 0)
			{
				conf->mode = client;
			}
			else
			{
				fprintf(stderr, "wrong mode at line: %d\n", line_num);
				fclose(f);
				return -1;
			}
		}
		else if (strcmp(key, "server") == 0)
		{
			_strncpy(conf->server, value, sizeof(conf->server));
		}
		else if (strcmp(key, "port") == 0)
		{
			_strncpy(conf->port, value, sizeof(conf->port));
		}
		else if (strcmp(key, "mtu") == 0)
		{
			conf->mtu = atoi(value);
			if (conf->mtu < 68 + IV_LEN)
			{
				fprintf(stderr, "mtu too small\n");
				fclose(f);
				return -1;
			}
			else if (conf->mtu > MTU_MAX)
			{
				fprintf(stderr, "mtu too large\n");
				fclose(f);
				return -1;
			}
		}
		else if (strcmp(key, "password") == 0)
		{
			md5(conf->key, value, strlen(value));
		}
		else if (strcmp(key, "tunif") == 0)
		{
			_strncpy(conf->tunif, value, sizeof(conf->tunif));
		}
		else if (strcmp(key, "up") == 0)
		{
			_strncpy(conf->up, value, sizeof(conf->up));
		}
		else if (strcmp(key, "down") == 0)
		{
			_strncpy(conf->down, value, sizeof(conf->down));
		}
	}
	fclose(f);

	return 0;
}

int parse_args(int argc, char **argv, conf_t *conf)
{
	const char *conf_file = NULL;

	bzero(conf, sizeof(conf_t));

	for (int i = 1; i < argc; i++)
	{
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0))
		{
			help();
			return -1;
		}
		else if ((strcmp(argv[i], "-c") == 0) || (strcmp(argv[i], "--config") == 0))
		{
			if (i + 2 > argc)
			{
				fprintf(stderr, "missing filename after '%s'\n", argv[i]);
				return 1;
			}
			conf_file = argv[i + 1];
			i++;
		}
		else if ((strcmp(argv[i], "-d") == 0) || (strcmp(argv[i], "--daemon") == 0))
		{
			conf->daemon = 1;
		}
		else if (strcmp(argv[i], "--pidfile") == 0)
		{
			if (i + 2 > argc)
			{
				fprintf(stderr, "missing filename after '%s'\n", argv[i]);
				return 1;
			}
			_strncpy(conf->pidfile, argv[i + 1], sizeof(conf->pidfile));
			i++;
		}
		else if (strcmp(argv[i], "--logfile") == 0)
		{
			if (i + 2 > argc)
			{
				fprintf(stderr, "missing filename after '%s'\n", argv[i]);
				return 1;
			}
			_strncpy(conf->logfile, argv[i + 1], sizeof(conf->logfile));
			i++;
		}
		else if ((strcmp(argv[i], "-V") == 0) || (strcmp(argv[i], "--version") == 0))
		{
			printf("%s %s\n", PACKAGE, VERSION);
			return -1;
		}
		else
		{
			fprintf(stderr, "invalid option: %s\n", argv[i]);
			return -1;
		}
	}
	if (conf_file == NULL)
	{
		fprintf(stderr, "config file not set\n");
		return -1;
	}
	else
	{
		if (read_conf(conf_file, conf) != 0)
		{
			return -1;
		}
	}

	if (conf->daemon)
	{
		if (conf->pidfile[0] == '\0')
		{
			fprintf(stderr, "pidfile not set\n");
			return -1;
		}
		if (conf->logfile[0] == '\0')
		{
			fprintf(stderr, "logfile not set\n");
			return -1;
		}
	}
	if (conf->mode == 0)
	{
		fprintf(stderr, "mode not set in config file\n");
		return -1;
	}
	if (conf->server[0] == '\0')
	{
		fprintf(stderr, "server not set in config file\n");
		return -1;
	}
	if (conf->port[0] == '\0')
	{
		strcpy(conf->port, "1205");
	}
	if (conf->tunif[0] == '\0')
	{
		strcpy(conf->tunif, "vpn0");
	}
	if (conf->mtu == 0)
	{
		conf->mtu = 1400;
	}
	if (conf->key[0] == '\0')
	{
		fprintf(stderr, "password not set in config file\n");
		return -1;
	}
	return 0;
}
