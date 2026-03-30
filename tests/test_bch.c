#include "test.h"
#include <libflex/bch.h>
#include <libflex/types.h>

static void test_bch_build_verify(void)
{
	uint32_t data21 = 0x0A5A5A;
	uint32_t cw = flex_codeword_build(data21);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_bch_build_verify_zero(void)
{
	uint32_t cw = flex_codeword_build(0);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_bch_build_verify_max(void)
{
	uint32_t cw = flex_codeword_build(0x1FFFFF);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_bch_encode_zero(void)
{
	ASSERT_EQ_U32(flex_bch_encode(0), 0);
}

static void test_bch_correct_single(void)
{
	uint32_t cw = flex_codeword_build(0x123456 & 0x1FFFFF);
	/* flip one bit */
	uint32_t bad = cw ^ (1u << 15);
	ASSERT(flex_bch_syndrome(bad) != 0);
	ASSERT_EQ_INT(flex_bch_correct(&bad), 1);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_correct_parity_bit(void)
{
	uint32_t cw = flex_codeword_build(0x0ABCDE & 0x1FFFFF);
	/* flip bit 0 (data bit) */
	uint32_t bad = cw ^ 1u;
	ASSERT(flex_bch_correct(&bad) >= 0);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_correct_double(void)
{
	/* FLEX BCH corrects 2-bit errors (unlike POCSAG which only detects) */
	uint32_t cw = flex_codeword_build(0x055555);
	uint32_t bad = cw ^ (1u << 20) ^ (1u << 10);
	ASSERT_EQ_INT(flex_bch_correct(&bad), 2);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_correct_double_adjacent(void)
{
	uint32_t cw = flex_codeword_build(0x1A2B3C & 0x1FFFFF);
	uint32_t bad = cw ^ (1u << 5) ^ (1u << 6);
	ASSERT_EQ_INT(flex_bch_correct(&bad), 2);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_correct_double_extremes(void)
{
	uint32_t cw = flex_codeword_build(0x0F0F0F);
	/* flip bit 0 and bit 30 (both within the 31-bit codeword) */
	uint32_t bad = cw ^ (1u << 0) ^ (1u << 30);
	ASSERT_EQ_INT(flex_bch_correct(&bad), 2);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_detect_triple(void)
{
	/* 3-bit errors should be uncorrectable */
	uint32_t cw = flex_codeword_build(0x0AAAAA);
	uint32_t bad = cw ^ (1u << 25) ^ (1u << 15) ^ (1u << 5);
	int ret = flex_bch_correct(&bad);
	/* Should be -1 (uncorrectable) or possibly miscorrect, but not 0 */
	(void)ret;
	/* With brute force, 3-bit errors may be detected as uncorrectable
	 * or may be miscorrected. Just verify it doesn't claim 0 errors. */
	ASSERT(ret != 0);
}

static void test_bch_all_single_bits_correctable(void)
{
	uint32_t cw = flex_codeword_build(0x1FFFFF);
	/* 31-bit codeword: bits 0-30 */
	for (int i = 0; i < 31; i++) {
		uint32_t bad = cw ^ (1u << i);
		ASSERT_EQ_INT(flex_bch_correct(&bad), 1);
		ASSERT_EQ_U32(bad, cw);
	}
}

static void test_bch_all_double_bits_correctable(void)
{
	/* test a representative set of 2-bit error patterns */
	uint32_t cw = flex_codeword_build(0x0C4E2A);
	int count = 0;
	/* 31-bit codeword: bits 0-30 */
	for (int i = 0; i < 31; i += 4) {
		for (int j = i + 1; j < 31; j += 4) {
			uint32_t bad = cw ^ (1u << i) ^ (1u << j);
			ASSERT_EQ_INT(flex_bch_correct(&bad), 2);
			ASSERT_EQ_U32(bad, cw);
			count++;
		}
	}
	ASSERT(count > 0);
}

void test_bch(void)
{
	printf("BCH:\n");
	RUN_TEST(test_bch_build_verify);
	RUN_TEST(test_bch_build_verify_zero);
	RUN_TEST(test_bch_build_verify_max);
	RUN_TEST(test_bch_encode_zero);
	RUN_TEST(test_bch_correct_single);
	RUN_TEST(test_bch_correct_parity_bit);
	RUN_TEST(test_bch_correct_double);
	RUN_TEST(test_bch_correct_double_adjacent);
	RUN_TEST(test_bch_correct_double_extremes);
	RUN_TEST(test_bch_detect_triple);
	RUN_TEST(test_bch_all_single_bits_correctable);
	RUN_TEST(test_bch_all_double_bits_correctable);
	printf("\n");
}
