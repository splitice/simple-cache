#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <unistd.h>
#include "http_parse.h"
#include "timer.h"
#include "debug.h"

volatile struct timeval current_time;
static int mon_fd;

static void timer_store_current_time()
{
	timeval _current_time;

	int err = gettimeofday(&_current_time, NULL);
	if (err == -1) {
		PFATAL("Failed to get system time");
	}
	memcpy((void*)&current_time, &_current_time, sizeof(current_time));
	//DEBUG("[#] Time is now %d\n", current_time.tv_sec);
}

static void timer_handler(int signum)
{
	uint64_t u = 1;

	// time accurate to the nearest 5ms
	timer_store_current_time();

	// check for monitoring updates
	if(monitoing_needs_to_run()){
		write(mon_fd, &u, sizeof(u));
	}	
}

void timer_setup(int evfd)
{
	struct sigaction sa;
	struct itimerval timer;

	mon_fd = evfd;

	/* Store current time */
	timer_store_current_time();

	/* Install timer_handler as the signal handler for SIGALRM. */
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction(SIGALRM, &sa, NULL);

	/* Configure the timer signal */
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 5000;//5ms
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 5000;//5ms

	/* Start a timer. */
	if (setitimer(ITIMER_REAL, &timer, NULL) < 0) {
		PFATAL("Error setting timer");
	}
}

void timer_cleanup() {
	struct itimerval timer;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &timer, NULL);
}