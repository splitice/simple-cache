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
#include "connection.h"
#include "hash.h"
#include "http.h"
#include "db.h"
#include "settings.h"
#include "timer.h"

int write_pid(char* pidFile, __pid_t pid){
	int fd;
	char buf[1024];

	fd = open(pidFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd == -1)
		PFATAL("Could not open PID file %s", pidFile);

	if (flock(fd, F_WRLCK) == -1) {
		if (errno == EAGAIN || errno == EACCES)
			PFATAL("PID file '%s' is locked; probably "
			"scache is already running", pidFile);
		else
			PFATAL("Unable to lock PID file '%s'", pidFile);
	}

	if (ftruncate(fd, 0) == -1)
		PFATAL("Could not truncate PID file '%s'", pidFile);

	snprintf(buf, 1024, "%ld\n", (long)getpid());
	if (write(fd, buf, strlen(buf)) != strlen(buf))
		PFATAL("Writing to PID file '%s'", pidFile);

	return fd;
}

/* Handler for Ctrl-C and related signals */
int extern stop_soon;
static void abort_handler(int sig) {
	if (stop_soon) exit(1);
	stop_soon = 1;
}

static void install_signal_handlers(){
	signal(SIGINT, abort_handler);
	signal(SIGTERM, abort_handler);
}

static int null_fd = -1;                /* File descriptor of /dev/null       */

static __pid_t fork_off() {

	__pid_t npid;

	fflush(0);

	npid = fork();

	if (npid < 0) PFATAL("fork() failed.");

	if (!npid) {
		/* Let's assume all this is fairly unlikely to fail, so we can live
		with the parent possibly proclaiming success prematurely. */

		if (dup2(null_fd, 0) < 0) PFATAL("dup2() failed.");

		/* If stderr is redirected to a file, keep that fd and use it for
		normal output. */

		if (isatty(2)) {

			if (dup2(null_fd, 1) < 0 || dup2(null_fd, 2) < 0)
				PFATAL("dup2() failed.");

		}
		else {

			if (dup2(2, 1) < 0) PFATAL("dup2() failed.");

		}

		close(null_fd);
		null_fd = -1;

		if (chdir("/")) PFATAL("chdir('/') failed.");

		setsid();

	}
	else {

		SAYF("[+] Daemon process created, PID %u (stderr %s).\n", npid,
			isatty(2) ? "not kept" : "kept as-is");

		SAYF("\nGood luck, you're on your own now!\n");
		sleep(1);
		exit(0);

	}

}

/* Time to go down the rabbit hole */
int main(int argc, char** argv)
{
	int pidfd;
	timer_setup();
	parse_arguments(argc, argv);

	__pid_t pid;
	if (settings.daemon_mode) {
		null_fd = open("/dev/null", O_RDONLY);
		if (null_fd < 0) PFATAL("Cannot open '/dev/null'.");
		pid = fork_off();
	}
	else{
		pid = getpid();
	}

	if (settings.pidfile){
		pidfd = write_pid(settings.pidfile, pid);
	}

	http_templates_init();
	db_open(settings.db_file_path);
	connection_open_listener();
	install_signal_handlers();

	//HTTP handler
	epoll_event_loop(http_connection_handler);

	connection_close_listener();

	if (settings.pidfile){
		close(pidfd);
		unlink(settings.pidfile);
	}
}