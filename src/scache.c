#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "config.h"
#include "debug.h"
#include "scache.h"
#include "hash.h"
#include "http.h"
#include "db.h"

/* Time to go down the rabbit hole */
int main()
{
	http_templates_init();
	db_open("/dbtest");
	connection_open_listener();
	epoll_event_loop();
}