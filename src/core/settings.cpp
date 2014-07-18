#include "settings.h"
#include <getopt.h>

struct scache_settings settings;

void parse_arguments(int argc, char** argv){
	int r;
    while ((r = getopt(argc, argv, "+LS:df:i:m:o:pr:s:t:u:b")) != -1) switch (r) {
            case 'L':
                break;
    }
}