#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "settings.h"

struct scache_settings settings {
	.max_size = 0,
	.db_file_path = NULL,
	.db_lru_clear = 0.1,
	.bind_af = AF_INET,
	.bind_addr = { 0 },
	.bind_port = 8000,
	.pidfile = NULL,
	.daemon_mode = false
};

static void print_usage(){
	printf("Usage: scache [ ...options... ]\n"
"\n"
"Networking options:\n"
"\n"
"  -4 / -6                                  - bind to either IPv4 or IPv6 address (default: IPv4)\n"
"  -p port  --bind-port port                - listen on the specified network port (default: 8000)\n"
"  -b addr  --bind-addr addr                - listen on the specified network address (default: 0.0.0.0)\n"
"  -p port  --bind-port port                - listen on the specified network port (default: 8000)\n"
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
"\n"
"Problems? You can reach the author at <admin@x4b.net>.\n");
}

void parse_arguments(int argc, char** argv){
	static struct option long_options[] =
	{
		/* These options set a flag. */
		/* These options set a value */
		{ "database-max-size", required_argument, 0, 's' },
		{ "database-file-path", required_argument, 0, 'd' },
		{ "database-lru-clear", required_argument, 0, 'l' },
		{ "bind-addr", required_argument, 0, 'b' },
		{ "bind-port", required_argument, 0, 'p' },
		{ 0, 0, 0, 0 }
	};

	//Defaults
	settings.bind_port = 8000;
	settings.db_file_path = "/var/lib/scache/";
	settings.max_size = 0;
	//By default bind to any IPv4
	settings.bind_af = AF_INET;
	memset(settings.bind_addr, sizeof(settings.bind_addr), sizeof(char));//INADDR_ANY

	int r = 0, option_index = 0;
	while ((r = getopt_long(argc, argv, "46dm:s:r:l:b:p:", long_options, &option_index)) != -1) {
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
		case 'm':
			settings.pidfile = optarg;
		case 's':
			settings.max_size = atol(optarg);
			break;
		case 'p':
			settings.bind_port = atoi(optarg);
			break;
		case 'r':
			settings.db_file_path = optarg;
			break;
		case 'b':
			inet_pton(settings.bind_af, optarg, settings.bind_addr);
			break;
		case 'l':
			settings.db_lru_clear = atof(optarg)/100;
			break;
		case '4': //flags
			settings.bind_af = AF_INET;
			break;
		case '6': //flags
			settings.bind_af = AF_INET6;
			break;
		default:
		case '?':
			print_usage();
			exit(EXIT_FAILURE);
		}
    }
}