#include "test.h"
#include <libflex/types.h>

extern int flex_alpha_encode(const char *text, size_t len,
                             uint32_t *chunks, int max_chunks);
extern int flex_alpha_decode(const uint32_t *chunks, int nchunks,
                             char *text, size_t text_cap);

static void test_alpha_roundtrip_short(void)
{
	uint32_t chunks[8];
	char buf[64];

	int nc = flex_alpha_encode("Hi", 2, chunks, 8);
	ASSERT(nc > 0);

	int len = flex_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "Hi");
}

static void test_alpha_roundtrip_long(void)
{
	uint32_t chunks[16];
	char buf[128];
	const char *msg = "Hello, World! This is FLEX.";
	size_t mlen = strlen(msg);

	int nc = flex_alpha_encode(msg, mlen, chunks, 16);
	ASSERT(nc > 0);

	int len = flex_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, msg);
}

static void test_alpha_special(void)
{
	uint32_t chunks[8];
	char buf[64];
	const char *msg = "Test@#$%";
	size_t mlen = strlen(msg);

	int nc = flex_alpha_encode(msg, mlen, chunks, 8);
	ASSERT(nc > 0);

	int len = flex_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, msg);
}

static void test_alpha_single_char(void)
{
	uint32_t chunks[4];
	char buf[16];

	int nc = flex_alpha_encode("A", 1, chunks, 4);
	ASSERT(nc > 0);

	int len = flex_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "A");
}

static void test_alpha_chunk_count(void)
{
	uint32_t chunks[16];

	/* 3 chars per chunk in FLEX alpha (21-bit words, 3x7-bit chars) */
	/* "Hi" = 2 chars -> 1 chunk (partial) */
	int nc = flex_alpha_encode("Hi", 2, chunks, 16);
	ASSERT_EQ_INT(nc, 1);

	/* "ABC" = 3 chars -> 1 chunk (exact) */
	nc = flex_alpha_encode("ABC", 3, chunks, 16);
	ASSERT_EQ_INT(nc, 1);

	/* "ABCD" = 4 chars -> 2 chunks */
	nc = flex_alpha_encode("ABCD", 4, chunks, 16);
	ASSERT_EQ_INT(nc, 2);
}

void test_alpha(void)
{
	printf("Alpha:\n");
	RUN_TEST(test_alpha_roundtrip_short);
	RUN_TEST(test_alpha_roundtrip_long);
	RUN_TEST(test_alpha_special);
	RUN_TEST(test_alpha_single_char);
	RUN_TEST(test_alpha_chunk_count);
	printf("\n");
}
