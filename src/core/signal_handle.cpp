#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include "debug.h"

/* Handler for Ctrl-C and related signals */
volatile extern sig_atomic_t stop_soon;
static void abort_handler(int sig) {
	if (stop_soon) {
		FATAL("Force exit, second signal received");
	}
	WARN("Shutting down pid %d due to %s", getpid(), strsignal(sig));
	stop_soon = 1;
}

void signal_handler_install() {
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, abort_handler);
	signal(SIGTERM, abort_handler);
}

void signal_handler_remove() {
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}