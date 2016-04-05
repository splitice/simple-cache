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

static const char * test_2x_rb_standard() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = BUFFER_SIZE+100;
	rb.write_position = BUFFER_SIZE+200;

	//Calculations
	mu_assert("test_2x_rb_standard rbuf_read_to_end check", rbuf_read_to_end(&rb) == (200 - 100));
	mu_assert("test_2x_rb_standard rbuf_read_remaining check", rbuf_read_remaining(&rb) == (200 - 100));
	mu_assert("test_2x_rb_standard rbuf_write_to_end check", rbuf_write_to_end(&rb) == (BUFFER_SIZE - 200));
	mu_assert("test_2x_rb_standard rbuf_write_remaining check", rbuf_write_remaining(&rb) == (BUFFER_SIZE - 100));

	return 0;
}

static const char * test_2x_rb_at_end_rollover_capacity() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = BUFFER_SIZE + 1000;
	rb.write_position = BUFFER_SIZE + BUFFER_SIZE;

	//Calculations
	mu_assert("test_2x_rb_at_end_rollover_capacity rbuf_read_to_end check", rbuf_read_to_end(&rb) == BUFFER_SIZE - 1000);
	mu_assert("test_2x_rb_at_end_rollover_capacity rbuf_read_remaining check", rbuf_read_remaining(&rb) == BUFFER_SIZE - 1000);
	mu_assert("test_2x_rb_at_end_rollover_capacity rbuf_write_to_end check", rbuf_write_to_end(&rb) == 1000);
	mu_assert("test_2x_rb_at_end_rollover_capacity rbuf_write_remaining check", rbuf_write_remaining(&rb) == 1000);
	mu_assert("test_2x_rb_at_end_rollover_capacity pointer check", RBUF_START(rb) == RBUF_WRITE(rb));

	return 0;
}

static const char * test_2x_rb_initial_empty_at_end() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = BUFFER_SIZE + BUFFER_SIZE;
	rb.write_position = BUFFER_SIZE + BUFFER_SIZE;

	//Calculations
	mu_assert("test_2x_rb_initial_empty_at_end rbuf_read_to_end check", rbuf_read_to_end(&rb) == 0);
	mu_assert("test_2x_rb_initial_empty_at_end rbuf_read_remaining check", rbuf_read_remaining(&rb) == 0);
	mu_assert("test_2x_rb_initial_empty_at_end rbuf_write_to_end check", rbuf_write_to_end(&rb) == BUFFER_SIZE);
	mu_assert("test_2x_rb_initial_empty_at_end rbuf_write_remaining check", rbuf_write_remaining(&rb) == BUFFER_SIZE);

	return 0;
}

static const char * test_high_rb() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 65000;
	rb.write_position = 1000;

	//Calculations
	mu_assert("test_high_rb rbuf_read_to_end check", rbuf_read_to_end(&rb) == (BUFFER_SIZE - (rb.read_position % BUFFER_SIZE)));
	mu_assert("test_high_rb rbuf_read_remaining check", rbuf_read_remaining(&rb) == (BUFFER_SIZE - (rb.read_position % BUFFER_SIZE)) + rb.write_position);
	mu_assert("test_high_rb rbuf_write_to_end check", rbuf_write_to_end(&rb) == BUFFER_SIZE - ((BUFFER_SIZE - (rb.read_position % BUFFER_SIZE)) + rb.write_position));
	mu_assert("test_high_rb rbuf_write_remaining check", rbuf_write_remaining(&rb) == BUFFER_SIZE - ((BUFFER_SIZE - (rb.read_position % BUFFER_SIZE)) + rb.write_position));

	return 0;
}

static const char * test_simple_rbcmp() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = BUFFER_SIZE - 5;
	rb.write_position = BUFFER_SIZE;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	mu_assert("test_simple_rbcmp rbuf_cmpn matches", rbuf_cmpn(&rb, "abc", 3) == 0);
	mu_assert("test_simple_rbcmp rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abd", 3) != 0);
	return 0;
}

static const char * test_rollover_rbcmp() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = BUFFER_SIZE - 5;
	rb.write_position = BUFFER_SIZE+2;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	rb.buffer[BUFFER_SIZE - 2] = 'd';
	rb.buffer[BUFFER_SIZE - 1] = 'e';
	rb.buffer[0] = 'f';
	rb.buffer[1] = 'g';

	mu_assert("test_rollover_rbcmp rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == 0);
	mu_assert("test_rollover_rbcmp rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) != 0);
	return 0;
}

static const char * test_rolloverhigh_rbcmp() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = (BUFFER_SIZE+BUFFER_SIZE) - 5;
	rb.write_position = (BUFFER_SIZE + BUFFER_SIZE) + 2;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	rb.buffer[BUFFER_SIZE - 2] = 'd';
	rb.buffer[BUFFER_SIZE - 1] = 'e';
	rb.buffer[0] = 'f';
	rb.buffer[1] = 'g';

	mu_assert("test_rolloverhigh_rbcmp rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == 0);
	mu_assert("test_rolloverhigh_rbcmp rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) != 0);
	return 0;
}

static const char * test_overflow_rbcmp() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 65536 - 5;
	rb.write_position = 2;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	rb.buffer[BUFFER_SIZE - 2] = 'd';
	rb.buffer[BUFFER_SIZE - 1] = 'e';
	rb.buffer[0] = 'f';
	rb.buffer[1] = 'g';

	mu_assert("test_overflow_rbcmp rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == 0);
	mu_assert("test_overflow_rbcmp rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) != 0);
	return 0;
}

static const char * test_regular_rbcmp_insufficient() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 65536 - 6;
	rb.write_position = 65535;

	rb.buffer[BUFFER_SIZE - 6] = 'a';
	rb.buffer[BUFFER_SIZE - 5] = 'b';
	rb.buffer[BUFFER_SIZE - 4] = 'c';

	rb.buffer[BUFFER_SIZE - 3] = 'd';
	rb.buffer[BUFFER_SIZE - 2] = 'e';

	mu_assert("test_regular_rbcmp_insufficient rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == -1);
	mu_assert("test_regular_rbcmp_insufficient rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) == -1);
	return 0;
}

static const char * test_overflow1_rbcmp_insufficient() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 65536 - 5;
	rb.write_position = 0;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	rb.buffer[BUFFER_SIZE - 2] = 'd';
	rb.buffer[BUFFER_SIZE - 1] = 'e';

	mu_assert("test_overflow1_rbcmp_insufficient rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == -1);
	mu_assert("test_overflow1_rbcmp_insufficient rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) == -1);
	return 0;
}

static const char * test_overflow2_rbcmp_insufficient() {
	struct read_buffer rb;
	rbuf_init(&rb);
	rb.read_position = 65536 - 5;
	rb.write_position = 1;

	rb.buffer[BUFFER_SIZE - 5] = 'a';
	rb.buffer[BUFFER_SIZE - 4] = 'b';
	rb.buffer[BUFFER_SIZE - 3] = 'c';

	rb.buffer[BUFFER_SIZE - 2] = 'd';
	rb.buffer[BUFFER_SIZE - 1] = 'e';
	rb.buffer[0] = 'f';

	mu_assert("test_overflow2_rbcmp_insufficient rbuf_cmpn matches", rbuf_cmpn(&rb, "abcdefg", 7) == -1);
	mu_assert("test_overflow2_rbcmp_insufficient rbuf_cmpn doesnt match", rbuf_cmpn(&rb, "abcdeeg", 7) == -1);
	return 0;
}


static const char * test_rbuffer() {
	//Basic tests
	mu_run_test(test_high_rb);
	mu_run_test(test_rb_initial);
	mu_run_test(test_rb_initial_full);
	mu_run_test(test_rb_standard);
	mu_run_test(test_rb_initial_empty_at_end);
	mu_run_test(test_rb_at_end_rollover_capacity);
	mu_run_test(test_2x_rb_standard);
	mu_run_test(test_2x_rb_at_end_rollover_capacity);
	mu_run_test(test_2x_rb_initial_empty_at_end);

	//Comparison tests
	mu_run_test(test_simple_rbcmp);
	mu_run_test(test_rollover_rbcmp);
	mu_run_test(test_rolloverhigh_rbcmp);
	mu_run_test(test_overflow_rbcmp);
	mu_run_test(test_regular_rbcmp_insufficient);
	mu_run_test(test_overflow1_rbcmp_insufficient);
	mu_run_test(test_overflow2_rbcmp_insufficient);
	return 0;
}