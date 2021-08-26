#include <stdio.h>
#include "minunit.h"
#include "tests_rbuffer.cpp"
#include "tests_system_simple.cpp"
#include <stdlib.h>     /* atoi */

int tests_run = 0;

#define TESTSET(x, func) result = func; \
if (result != 0) { \
	printf("[%s] Test Set Failure: %s\n", x, result); \
}\
else { \
	printf("[%s] ALL TESTS PASSED\n", x); \
} \
printf("[%s] Tests run: %d\n", x, tests_run); \
final_result |= (result != 0);


int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);

	int final_result = 0;
	const char *result;
	TESTSET("read_buffer", test_rbuffer());
	return final_result;
}