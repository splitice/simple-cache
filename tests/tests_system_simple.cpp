#include "minunit.h"
#include "scenario.h"

extern int tests_run;
static const char* binary_path;
static const char* testcases_path;
static int port;

static const char * test_simple_put_get() {
	bool result = false;

	result |= !run_scenarios(binary_path, "simple", testcases_path, port ? port : 8001, port == 0, "");
	result |= !run_scenarios(binary_path, "multiple-connections", testcases_path, port ? port : 8001, port == 0, "");
	//result |= !run_scenarios(binary_path, "lru", testcases_path, port ? port : 8002, port == 0, "--database-max-size 5000", false);
	result |= !run_scenarios(binary_path, "expires", testcases_path, port ? port : 8001, port == 0, "");
	result |= !run_scenarios(binary_path, "table", testcases_path, port ? port : 8001, port == 0, "");
	result |= !run_scenarios(binary_path, "complex-system", testcases_path, port ? port : 8001, port == 0, "");

	return result ? "System Tests failed" : 0;
}

static const char * test_simple(const char* a_binary_path, const char* a_testcases_path, int a_port) {
	binary_path = a_binary_path;
	testcases_path = a_testcases_path;
	port = a_port;
	mu_run_test(test_simple_put_get);
	return 0;
}