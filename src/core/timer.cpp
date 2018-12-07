#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "debug.h"

volatile time_t time_seconds;

static void timer_store_current_time()
{
	timeval current_time;
	int err = gettimeofday(&current_time, NULL);
	if (err == -1) {
		PFATAL("Failed to get system time");
	}
	time_seconds = current_time.tv_sec;
	//DEBUG("[#] Time is now %d\n", current_time.tv_sec);
}

static void timer_handler(int signum)
{
	timer_store_current_time();
}

void timer_setup()
{
	struct sigaction sa;
	struct itimerval timer;

	/* Store current time */
	timer_store_current_time();

	/* Install timer_handler as the signal handler for SIGALRM. */
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction(SIGALRM, &sa, NULL);

	/* Configure the timer signal */
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 500000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 500000;

	/* Start a timer. */
	if (setitimer(ITIMER_REAL, &timer, NULL) < 0) {
		PFATAL("Error setting timer");
	}
}