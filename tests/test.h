#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int test_pass;
extern int test_fail;

#define RUN_TEST(fn) do { \
	int _pf = test_fail; \
	printf("  %-55s", #fn); \
	fn(); \
	if (test_fail == _pf) { printf(" PASS\n"); test_pass++; } \
} while(0)

#define FAIL(msg) do { \
	printf(" FAIL\n    %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
	test_fail++; \
	return; \
} while(0)

#define ASSERT(cond) do { \
	if (!(cond)) FAIL(#cond); \
} while(0)

#define ASSERT_EQ_U32(a, b) do { \
	uint32_t _a = (a), _b = (b); \
	if (_a != _b) { \
		printf(" FAIL\n    expected 0x%08X, got 0x%08X (%s:%d)\n", \
		       (unsigned)_b, (unsigned)_a, __FILE__, __LINE__); \
		test_fail++; \
		return; \
	} \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
	int _a = (a), _b = (b); \
	if (_a != _b) { \
		printf(" FAIL\n    expected %d, got %d (%s:%d)\n", \
		       _b, _a, __FILE__, __LINE__); \
		test_fail++; \
		return; \
	} \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
	const char *_a = (a), *_b = (b); \
	if (strcmp(_a, _b) != 0) { \
		printf(" FAIL\n    expected \"%s\", got \"%s\" (%s:%d)\n", \
		       _b, _a, __FILE__, __LINE__); \
		test_fail++; \
		return; \
	} \
} while(0)

#endif
