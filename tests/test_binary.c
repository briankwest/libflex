#include "test.h"
#include <libflex/types.h>

extern int flex_binary_encode(const uint8_t *data, size_t len, int bit_width,
                              uint32_t *chunks, int max_chunks);
extern int flex_binary_decode(const uint32_t *chunks, int nchunks, int bit_width,
                              uint8_t *data, size_t data_cap, size_t *data_len);

static void test_binary_roundtrip_8bit(void)
{
	uint8_t data[] = { 0x41, 0x42 };  /* 'A', 'B' */
	uint32_t chunks[8];

	/* 8-bit width: 21/8 = 2 items per chunk */
	int nc = flex_binary_encode(data, 2, 8, chunks, 8);
	ASSERT_EQ_INT(nc, 1);

	uint8_t out[8];
	size_t out_len = 0;
	int ret = flex_binary_decode(chunks, nc, 8, out, sizeof(out), &out_len);
	ASSERT(ret > 0);
	ASSERT_EQ_INT((int)out_len, 2);
	ASSERT_EQ_INT(out[0], 0x41);
	ASSERT_EQ_INT(out[1], 0x42);
}

static void test_binary_roundtrip_7bit(void)
{
	uint8_t data[] = { 0x48, 0x65, 0x6C };  /* 'H', 'e', 'l' */
	uint32_t chunks[8];

	/* 7-bit width: 21/7 = 3 items per chunk */
	int nc = flex_binary_encode(data, 3, 7, chunks, 8);
	ASSERT_EQ_INT(nc, 1);

	uint8_t out[8];
	size_t out_len = 0;
	flex_binary_decode(chunks, nc, 7, out, sizeof(out), &out_len);
	ASSERT_EQ_INT((int)out_len, 3);
	ASSERT_EQ_INT(out[0], 0x48);
	ASSERT_EQ_INT(out[1], 0x65);
	ASSERT_EQ_INT(out[2], 0x6C);
}

static void test_binary_roundtrip_1bit(void)
{
	/* 1-bit width: 21 items per chunk */
	uint8_t data[21];
	for (int i = 0; i < 21; i++)
		data[i] = i & 1;

	uint32_t chunks[4];
	int nc = flex_binary_encode(data, 21, 1, chunks, 4);
	ASSERT_EQ_INT(nc, 1);

	uint8_t out[32];
	size_t out_len = 0;
	flex_binary_decode(chunks, nc, 1, out, sizeof(out), &out_len);
	ASSERT_EQ_INT((int)out_len, 21);
	for (int i = 0; i < 21; i++)
		ASSERT_EQ_INT(out[i], i & 1);
}

static void test_binary_multi_chunk(void)
{
	uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
	uint32_t chunks[8];

	/* 8-bit: 2 per chunk -> 5 items needs 3 chunks */
	int nc = flex_binary_encode(data, 5, 8, chunks, 8);
	ASSERT_EQ_INT(nc, 3);

	uint8_t out[8];
	size_t out_len = 0;
	flex_binary_decode(chunks, nc, 8, out, sizeof(out), &out_len);
	/* decodes 2 items per chunk * 3 chunks = 6, but we only encoded 5 */
	/* the 6th item will be 0 (padding) */
	ASSERT(out_len >= 5);
	ASSERT_EQ_INT(out[0], 0x01);
	ASSERT_EQ_INT(out[1], 0x02);
	ASSERT_EQ_INT(out[2], 0x03);
	ASSERT_EQ_INT(out[3], 0x04);
	ASSERT_EQ_INT(out[4], 0x05);
}

static void test_binary_invalid_width(void)
{
	uint8_t data[] = { 0x01 };
	uint32_t chunks[4];
	ASSERT_EQ_INT(flex_binary_encode(data, 1, 0, chunks, 4), -1);
	ASSERT_EQ_INT(flex_binary_encode(data, 1, 17, chunks, 4), -1);
}

static void test_binary_16bit(void)
{
	/* 16-bit width: 21/16 = 1 item per chunk */
	uint8_t data[] = { 0xFF, 0xAB };  /* only low 16 bits used, but input is uint8 */
	uint32_t chunks[8];

	int nc = flex_binary_encode(data, 2, 16, chunks, 8);
	ASSERT_EQ_INT(nc, 2);  /* 1 item per chunk, 2 items */

	uint8_t out[4];
	size_t out_len = 0;
	flex_binary_decode(chunks, nc, 16, out, sizeof(out), &out_len);
	ASSERT_EQ_INT((int)out_len, 2);
}

void test_binary(void)
{
	printf("Binary:\n");
	RUN_TEST(test_binary_roundtrip_8bit);
	RUN_TEST(test_binary_roundtrip_7bit);
	RUN_TEST(test_binary_roundtrip_1bit);
	RUN_TEST(test_binary_multi_chunk);
	RUN_TEST(test_binary_invalid_width);
	RUN_TEST(test_binary_16bit);
	printf("\n");
}
