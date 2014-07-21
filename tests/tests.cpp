#include <stdio.h>
#include "minunit.h"
#include "tests_rbuffer.cpp"
#include "tests_system_simple.cpp"

int tests_run = 0;

#define TESTSET(x, func) result = func; \
if (result != 0) { \
	printf("[%s] Assertion Failure: %s\n", x, result); \
}\
else { \
	printf("[%s] ALL TESTS PASSED\n", x); \
} \
printf("[%s] Tests run: %d\n", x, tests_run); \
final_result |= (result != 0);


int main(int argc, char *argv[])
{
	if (argc != 3){
		printf("Usage: tests [server binary] [test case path]\n");
		return 2;
	}

	int final_result = 0;
	const char *result;
	TESTSET("read_buffer", test_rbuffer());
	TESTSET("system_simple", test_simple(argv[1],argv[2]));
	return final_result != 0;
}