#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "debug.h"
#include "settings.h"

struct scache_settings settings {
	.max_size = 0,
	.db_file_path = NULL,
	.db_lru_clear = 0.2,
	.bind = NULL,
	.bind_num = 0,
	.pidfile = NULL,
	.daemon_mode = false,
	.daemon_output = false
};

static void print_usage() {
	printf("Usage: scache [ ...options... ]\n"
"\n"
"Networking options:\n"
"\n"
"  -4 / -6                                  - bind to either IPv4 or IPv6 address (default: IPv4)\n"
"  -b addr  --bind addr:port                - listen on the specified network address (default: none)\n"
"\n"
"Database settings:\n"
"\n"
"  -s bytes  --database-max-size bytes      - max size to be maintained by LRU (default: 0)\n"
"  -r path  --database-file-path path       - where to store data (default: /var/lib/scache/)\n"
"  -l percent  --database-lru-clear percent - percentage of database to free during LRU cleanup (default: 10)\n"
"\n"
"General settings:\n"
"\n"
"  -m file  --make-pid file                 - output a PID file (default: no)\n"
"  -d                                       - daemonize (default: no)\n"
"  -o                                       - redirect output to /dev/null if daemonized (default: yes)\n"
"\n"
"Problems? You can reach the author at <admin@x4b.net>.\n");
}

char* string_allocate(const char* s)
{
	char* c = (char*)malloc(strlen(s) + 1);
	strcpy(c, s);
	return c;
}

enum bind_parse_state
{
	first, ipv4, ipv6, unix_path, port
};
static void parse_binds(const char* optarg_const)
{
	char* optarg = strdup(optarg_const);
	int len = strlen(optarg);
	bind_parse_state state = bind_parse_state::first;
	int state_start = 0;
	int num = 0;
	for (int i = 0; i <= len; i++)
	{
		if (optarg[i] == ',' || optarg[i] == 0)
		{
			num++;
		}	
	}
	settings.bind_num = num;
	settings.bind = (scache_bind*)malloc(sizeof(scache_bind) * num);
	memset(settings.bind, 0, sizeof(scache_bind) * num);
	scache_bind* current = settings.bind;
	for (int i = 0; i <= len; i++)
	{
		char copy = optarg[i];
		switch (state)
		{
		case bind_parse_state::first:
			if (optarg[i] == '[')
			{
				current->af = AF_INET6;
				state = bind_parse_state::ipv6;
				state_start++;
				break;
			}
			else if (optarg[i] == 'u' && optarg[i+1] == ':')
			{
				current->af = AF_UNIX;
				state = bind_parse_state::unix_path;
				state_start+=2;
				i++;
				break;
			}
			else
			{
				current->af = AF_INET;
				state = bind_parse_state::ipv4;
			}
		case bind_parse_state::ipv6:
		case bind_parse_state::ipv4:
			if (optarg[i] == ':')
			{
				optarg[i] = 0;
				if (inet_pton(current->af, optarg + state_start, current->addr) < 0) {
					PFATAL("Error parsing bind address");
				}
				state = bind_parse_state::port;
				state_start = i + 1;
			}
			break;
		case bind_parse_state::unix_path:
			if (optarg[i] == 0 || optarg[i] == ',')
			{
				memcpy(current->addr, optarg + state_start, i - state_start);
				current->port = 0;
				if (optarg[i] == ',')
				{
					current++;
					state = bind_parse_state::first;
					state_start = i + 1;				
				}
			}
			break;
		case bind_parse_state::port:
			
			if (optarg[i] == 0 || optarg[i] == ',')
			{
				optarg[i] = 0;
				current->port = atoi(optarg + state_start);
				if (copy == ',')
				{
					current++;
					state = bind_parse_state::first;
					state_start = i + 1;				
				}
			}
			
			break;
		}
	}
		
	free(optarg);
}

void settings_parse_arguments(int argc, char** argv) {
	static struct option long_options[] =
	{
		/* These options set a flag. */
		/* These options set a value */
		{ "make-pid", required_argument, 0, 'm' },
		{ "database-max-size", required_argument, 0, 's' },
		{ "database-file-path", required_argument, 0, 'r' },
		{ "database-lru-clear", required_argument, 0, 'l' },
		{ "bind", required_argument, 0, 'b' },
		{ 0, 0, 0, 0 }
	};

	//Defaults
	settings.db_file_path = NULL;

	int r = 0, option_index = 0;
	while ((r = getopt_long(argc, argv, "dom:s:r:l:b:", long_options, &option_index)) != -1) {
		switch (r) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
			printf("option %s", long_options[option_index].name);
			if (optarg)
				printf(" with arg %s", optarg);
			printf("\n");
			break;
		case 'd':
			settings.daemon_mode = true;
			break;
		case 'o':
			settings.daemon_output = true;
			break;
		case 'm':
			settings.pidfile = optarg; 
			break;
		case 's':
			settings.max_size = atol(optarg);
			break;
		case 'r':
			settings.db_file_path = optarg;
			break;
		case 'b':
			parse_binds(optarg);
			break;
		case 'l':
			settings.db_lru_clear = atof(optarg)/100;
			break;
		default:
		case '?':
			print_usage();
			exit(EXIT_FAILURE);
		}
    }

	if (settings.db_file_path == NULL) {
		settings.db_file_path = "/var/lib/scache/";
	}
}

void settings_cleanup() {
}