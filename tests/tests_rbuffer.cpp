#include "read_buffer.h"
#include "minunit.h"

extern int tests_run;

static const char * test_rb_initial() {
	struct read_buffer rb;
	rbuf_init(&rb);

	//Init
	mu_assert("rbuf_init should set read_position to 0", rb.read_position == 0);
	mu_assert("rbuf_init should set write_position to 0", rb.write_position == 0);

	//Calculations
	mu_assert("test_rb_initial rbuf_read_to_end check", rbuf_read_to_end(&rb) == 0);
	mu_assert("test_rb_initial rbuf_read_remaining check", rbuf_read_remaining(&rb) == 0);
	mu_assert("test_rb_initial rbuf_write_to_end check", rbuf_write_to_end(&rb) == BUFFER_SIZE);
	mu_assert("test_rb_initial rbuf_write_remaining check", rbuf_write_remaining(&rb) == BUFFER_SIZE);

	return 0;
}

static const char * test_rb_initial_full() {
	struct read_buffer rb;
	rbuf_init(&rb);
	RBUF_WRITEMOVE(rb, BUFFER_SIZE);

	//Calculations
	mu_assert("test_rb_initial_full rbuf_read_to_end check", rbuf_read_to_end(&rb) == BUFFER_SIZE);
	mu_assert("test_rb_initial_full rbuf_read_remaining check", rbuf_read_remaining(&rb) == BUFFER_SIZE);
	mu_assert("test_rb_initial_full rbuf_write_to_end check", rbuf_write_to_end(&rb) == 0);
	mu_assert("test_rb_initial_full rbuf_write_remaining check", rbuf_write_remaining(&rb) == 0);

	return 0;
}

static const char * test_rb_initial_empty_at_end() {
	struct read_buffer rb;
	rbuf_init(&rb);
	RBUF_WRITEMOVE(rb, BUFFER_SIZE);
	RBUF_READMOVE(rb, BUFFER_SIZE);

	//Calculations
	mu_assert("test_rb_initial_empty_at_end rbuf_read_to_end check", rbuf_read_to_end(&rb) == 0);
	mu_assert("test_rb_initial_empty_at_end rbuf_read_remaining check", rbuf_read_remaining(&rb) == 0);
	mu_assert("test_rb_initial_empty_at_end rbuf_write_to_end check", rbuf_write_to_end(&rb) == BUFFER_SIZE);
	mu_assert("test_rb_initial_empty_at_end rbuf_write_remaining check", rbuf_write_remaining(&rb) == BUFFER_SIZE);

	return 0;
}

static const char * test_rb_at_end_rollover_capacity() {
	struct read_buffer rb;
	rbuf_init(&rb);
	RBUF_WRITEMOVE(rb, BUFFER_SIZE);
	RBUF_READMOVE(rb, 1000);

	//Calculations
	mu_assert("test_rb_at_end_rollover_capacity rbuf_read_to_end check", rbuf_read_to_end(&rb) == BUFFER_SIZE - 1000);
	mu_assert("test_rb_at_end_rollover_capacity rbuf_read_remaining check", rbuf_read_remaining(&rb) == BUFFER_SIZE - 1000);
	mu_assert("test_rb_at_end_rollover_capacity rbuf_write_to_end check", rbuf_write_to_end(&rb) == 1000);
	mu_assert("test_rb_at_end_rollover_capacity rbuf_write_remaining check", rbuf_write_remaining(&rb) == 1000);
	mu_assert("test_rb_at_end_rollover_capacity pointer check", RBUF_START(rb) == RBUF_WRITE(rb));

	return 0;
}

static const char * test_rb_standard() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 100;
	rb.write_position = 200;

	//Calculations
	mu_assert("test_rb_standard rbuf_read_to_end check", rbuf_read_to_end(&rb) == (200 - 100));
	mu_assert("test_rb_standard rbuf_read_remaining check", rbuf_read_remaining(&rb) == (200 - 100));
	mu_assert("test_rb_standard rbuf_write_to_end check", rbuf_write_to_end(&rb) == (BUFFER_SIZE - 200));
	mu_assert("test_rb_standard rbuf_write_remaining check", rbuf_write_remaining(&rb) == (BUFFER_SIZE - 100));

	return 0;
}

static const char * test_rbuffer() {
	mu_run_test(test_rb_initial);
	mu_run_test(test_rb_initial_full);
	mu_run_test(test_rb_standard);
	mu_run_test(test_rb_initial_empty_at_end);
	mu_run_test(test_rb_at_end_rollover_capacity);
	return 0;
}