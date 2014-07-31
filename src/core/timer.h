#include <atomic>

volatile extern std::atomic<time_t> time_seconds;

void timer_setup();