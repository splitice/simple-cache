#include <stdio.h>
#include "minunit.h"
#include "tests_rbuffer.cpp"

int tests_run = 0;

#define TESTSET(x, func) result = func(); \
if (result != 0) { \
	printf("[%s] Assertion Failure: %s\n", x, result); \
}\
else { \
	printf("[%s] ALL TESTS PASSED\n", x); \
} \
printf("[%s] Tests run: %d\n", x, tests_run); \
final_result |= (result != 0);

static const char * all_tests() {
	test_rbuffer();
}

int main(int argc, char *argv[])
{
	int final_result = 0;
	const char *result;
	TESTSET("read_buffer", test_rbuffer);
	return final_result != 0;
}