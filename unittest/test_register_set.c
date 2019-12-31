#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/chunk.h"

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

int main( __attribute__((unused)) int argc, __attribute__((unused)) char** argv) {
	test_register_op();
	test_register_a();
	test_register_b();
	test_register_c();
}
