#include <stdio.h>
#include "minunit.h"
#include "read_buffer.h"

int tests_run = 0;

static const char * test_rb_initial() {
	struct read_buffer rb;
	rbuf_init(&rb);

	//Init
	mu_assert("rbuf_init should set read_position to 0", rb.read_position == 0);
	mu_assert("rbuf_init should set write_position to 0", rb.write_position == 0);

	//Calculations
	mu_assert("rbuf_read_to_end check", rbuf_read_to_end(&rb) == 0);
	mu_assert("rbuf_read_remaining check", rbuf_read_remaining(&rb) == 0);
	mu_assert("rbuf_write_to_end check", rbuf_write_to_end(&rb) == BUFFER_SIZE);
	mu_assert("rbuf_write_remaining check", rbuf_write_remaining(&rb) == BUFFER_SIZE);

	return 0;
}

static const char * all_tests() {
	mu_run_test(test_rb_initial);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *result = all_tests();
	if (result != 0) {
		printf("%s\n", result);
	}
	else {
		printf("ALL TESTS PASSED\n");
	}
	printf("Tests run: %d\n", tests_run);

	return result != 0;
}