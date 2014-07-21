#include "minunit.h"
#include "scenario.h"

extern int tests_run;
static const char* binary_path;
static const char* testcases_path;

static const char * test_simple_put_get() {
	bool result = false;

	result |= run_scenario(binary_path, testcases_path, "1-put-get.txt", 8001);

	return result ? "System Tests failed" : 0;
}

static const char * test_simple(const char* a_binary_path, const char* a_testcases_path) {
	binary_path = a_binary_path;
	testcases_path = a_testcases_path;
	mu_run_test(test_simple_put_get);
	return 0;
}