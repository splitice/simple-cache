#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "settings.h"

struct scache_settings settings;

static void print_usage(){
	printf("TODO\r\n");
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
	settings.db_file_path = "/dbtest";
	settings.db_lru_clear = 0.1;
	settings.max_size = 0;
	//By default bind to any IPv4
	settings.bind_af = AF_INET;
	memset(settings.bind_addr, sizeof(settings.bind_addr), sizeof(char));//INADDR_ANY

	int r = 0, option_index = 0;
	while ((r = getopt_long(argc, argv, "46s:d:l:b:p:", long_options, &option_index)) != -1) {

		switch (r) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
			printf("option %s", long_options[option_index].name);
			if (optarg)
				printf(" with arg %s", optarg);
			printf("\n");
			break;

		case 's':
			settings.max_size = atol(optarg);
			break;
		case 'p':
			settings.bind_port = atoi(optarg);
			break;
		case 'd':
			settings.db_file_path = optarg;
			break;
		case 'b':
			inet_pton(settings.bind_af, optarg, settings.bind_addr);
			break;
		case 'l':
			settings.db_lru_clear = atof(optarg);
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