#include "test.h"
#include <libflex/fiw.h>
#include <libflex/bch.h>

static void test_fiw_roundtrip(void)
{
	flex_fiw_t in = { .cycle = 7, .frame = 64, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	/* codeword should be BCH-valid */
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));

	flex_fiw_t out;
	ASSERT_EQ_INT(flex_fiw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.cycle, 7);
	ASSERT_EQ_INT(out.frame, 64);
}

static void test_fiw_roundtrip_extremes(void)
{
	/* max valid values */
	flex_fiw_t in = { .cycle = 14, .frame = 127, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	flex_fiw_t out;
	ASSERT_EQ_INT(flex_fiw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.cycle, 14);
	ASSERT_EQ_INT(out.frame, 127);
}

static void test_fiw_roundtrip_zero(void)
{
	flex_fiw_t in = { .cycle = 0, .frame = 0, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	flex_fiw_t out;
	ASSERT_EQ_INT(flex_fiw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.cycle, 0);
	ASSERT_EQ_INT(out.frame, 0);
}

static void test_fiw_bch_correction(void)
{
	flex_fiw_t in = { .cycle = 3, .frame = 42, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	/* corrupt 1 bit */
	uint32_t bad = cw ^ (1u << 20);

	flex_fiw_t out;
	ASSERT_EQ_INT(flex_fiw_decode(bad, &out), FLEX_OK);
	ASSERT_EQ_INT(out.cycle, 3);
	ASSERT_EQ_INT(out.frame, 42);
}

static void test_fiw_bch_correction_2bit(void)
{
	flex_fiw_t in = { .cycle = 10, .frame = 100, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	/* corrupt 2 bits */
	uint32_t bad = cw ^ (1u << 25) ^ (1u << 5);

	flex_fiw_t out;
	ASSERT_EQ_INT(flex_fiw_decode(bad, &out), FLEX_OK);
	ASSERT_EQ_INT(out.cycle, 10);
	ASSERT_EQ_INT(out.frame, 100);
}

static void test_fiw_uncorrectable(void)
{
	flex_fiw_t in = { .cycle = 5, .frame = 50, .repeat = 0 };
	uint32_t cw = flex_fiw_encode(&in);

	/* corrupt 3 bits -- should fail */
	uint32_t bad = cw ^ (1u << 25) ^ (1u << 15) ^ (1u << 5);

	flex_fiw_t out;
	ASSERT(flex_fiw_decode(bad, &out) != FLEX_OK);
}

static void test_fiw_all_cycles(void)
{
	for (uint16_t c = 0; c <= 14; c++) {
		flex_fiw_t in = { .cycle = c, .frame = 0, .repeat = 0 };
		uint32_t cw = flex_fiw_encode(&in);

		flex_fiw_t out;
		ASSERT_EQ_INT(flex_fiw_decode(cw, &out), FLEX_OK);
		ASSERT_EQ_INT(out.cycle, c);
	}
}

void test_fiw(void)
{
	printf("FIW:\n");
	RUN_TEST(test_fiw_roundtrip);
	RUN_TEST(test_fiw_roundtrip_extremes);
	RUN_TEST(test_fiw_roundtrip_zero);
	RUN_TEST(test_fiw_bch_correction);
	RUN_TEST(test_fiw_bch_correction_2bit);
	RUN_TEST(test_fiw_uncorrectable);
	RUN_TEST(test_fiw_all_cycles);
	printf("\n");
}
