#include <stdio.h>
#include <stdlib.h>

#include "../src/chunk.h"

#undef NDEBUG
#include <assert.h>

#define assert_equal(a, b) if((a) != (b)) {\
	fprintf(stderr, "Assertion that '%d' equals '%d' failed.\n", a, b);\
	exit(2);\
}

void test_register_op(void) {
	for(uint8_t i = 0; i < UINT8_MAX; i++) {
		uint32_t data = 0;
		setbc_op(&data, i);
		Reg op = OP(data);
		assert(op == i);
	}
}

void test_register_a(void) {
	for(uint8_t i = 0; i < UINT8_MAX; i++) {
		uint32_t data = 0;
		setbc_a(&data, i);
		Reg a = RA(data);
		assert(a == i);
	}
}

void test_register_b(void) {
	for(uint8_t i = 0; i < UINT8_MAX; i++) {
		uint32_t data = 0;
		setbc_b(&data, i);
		Reg b = RB(data);
		assert_equal(b, i);
	}
}

void test_register_c(void) {
	for(uint8_t i = 0; i < UINT8_MAX; i++) {
		uint32_t data = 0;
		setbc_c(&data, i);
		Reg c = RC(data);
		assert(c == i);
	}
}

void test_register_d(void) {
	for(uint16_t i = 0; i < UINT16_MAX; i++) {
		uint32_t data = 0;
		setbc_d(&data, i);
		uint16_t d = RD(data);
		if(d != i) {
			fprintf(stderr, "d = %u\ni = %u\n", d, i);
		}
		assert(d == i);
	}
}

/*
void test_register_j(void) {
	for(uint16_t i = 0; i < UINT16_MAX; i++) {
		uint32_t data = 0;
		setbc_d(&data, i);
		uint16_t d = RD(data);
		if(d != i) {
			fprintf(stderr, "d = %u\ni = %u\n", d, i);
		}
		assert(d == i);
	}
}
*/

int main( __attribute__((unused)) int argc, __attribute__((unused)) char** argv) {
	test_register_op();
	test_register_a();
	test_register_b();
	test_register_c();
	test_register_d();
}
