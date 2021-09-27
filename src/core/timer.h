volatile extern struct timeval current_time;

void timer_setup(int evfd);
void timer_cleanup();