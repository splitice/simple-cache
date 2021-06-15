#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "timer.h"
#include "debug.h"

volatile struct timeval current_time;

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

void monitoring_check();

static void timer_handler(int signum)
{
	// time accurate to the nearest 5ms
	timer_store_current_time();

	// check for monitoring updates
	monitoring_check();
	
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
	timer.it_value.tv_usec = 5000;//5ms
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 5000;//5ms

	/* Start a timer. */
	if (setitimer(ITIMER_REAL, &timer, NULL) < 0) {
		PFATAL("Error setting timer");
	}
}