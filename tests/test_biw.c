#include "test.h"
#include <libflex/biw.h>
#include <libflex/bch.h>

static void test_biw_roundtrip(void)
{
	flex_biw_t in = {
		.priority_addr = 2,
		.addr_field_start = 2,  /* stored as value-1 in bits */
		.vect_field_start = 10,
		.carry_on = 0
	};
	uint32_t cw = flex_biw_encode(&in);

	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));

	flex_biw_t out;
	ASSERT_EQ_INT(flex_biw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.priority_addr, 2);
	ASSERT_EQ_INT(out.addr_field_start, 2);
	ASSERT_EQ_INT(out.vect_field_start, 10);
	ASSERT_EQ_INT(out.carry_on, 0);
}

static void test_biw_roundtrip_max(void)
{
	flex_biw_t in = {
		.priority_addr = 15,
		.addr_field_start = 4,  /* max: bits 8-9 = 3, +1 = 4 */
		.vect_field_start = 63, /* max: 6 bits */
		.carry_on = 31          /* max: 5 bits */
	};
	uint32_t cw = flex_biw_encode(&in);

	flex_biw_t out;
	ASSERT_EQ_INT(flex_biw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.priority_addr, 15);
	ASSERT_EQ_INT(out.addr_field_start, 4);
	ASSERT_EQ_INT(out.vect_field_start, 63);
	ASSERT_EQ_INT(out.carry_on, 31);
}

static void test_biw_roundtrip_min(void)
{
	flex_biw_t in = {
		.priority_addr = 0,
		.addr_field_start = 1,  /* min: bits 8-9 = 0, +1 = 1 */
		.vect_field_start = 0,
		.carry_on = 0
	};
	uint32_t cw = flex_biw_encode(&in);

	flex_biw_t out;
	ASSERT_EQ_INT(flex_biw_decode(cw, &out), FLEX_OK);
	ASSERT_EQ_INT(out.priority_addr, 0);
	ASSERT_EQ_INT(out.addr_field_start, 1);
	ASSERT_EQ_INT(out.vect_field_start, 0);
}

static void test_biw_bch_correction(void)
{
	flex_biw_t in = {
		.priority_addr = 5,
		.addr_field_start = 3,
		.vect_field_start = 20,
		.carry_on = 1
	};
	uint32_t cw = flex_biw_encode(&in);

	/* corrupt 1 bit */
	uint32_t bad = cw ^ (1u << 18);

	flex_biw_t out;
	ASSERT_EQ_INT(flex_biw_decode(bad, &out), FLEX_OK);
	ASSERT_EQ_INT(out.priority_addr, 5);
	ASSERT_EQ_INT(out.addr_field_start, 3);
	ASSERT_EQ_INT(out.vect_field_start, 20);
}

static void test_biw_bch_correction_2bit(void)
{
	flex_biw_t in = {
		.priority_addr = 8,
		.addr_field_start = 2,
		.vect_field_start = 30,
		.carry_on = 0
	};
	uint32_t cw = flex_biw_encode(&in);

	/* corrupt 2 bits */
	uint32_t bad = cw ^ (1u << 28) ^ (1u << 3);

	flex_biw_t out;
	ASSERT_EQ_INT(flex_biw_decode(bad, &out), FLEX_OK);
	ASSERT_EQ_INT(out.vect_field_start, 30);
}

static void test_biw_uncorrectable(void)
{
	flex_biw_t in = {
		.priority_addr = 1,
		.addr_field_start = 1,
		.vect_field_start = 5,
		.carry_on = 0
	};
	uint32_t cw = flex_biw_encode(&in);

	/* corrupt 3 bits */
	uint32_t bad = cw ^ (1u << 30) ^ (1u << 20) ^ (1u << 10);

	flex_biw_t out;
	ASSERT(flex_biw_decode(bad, &out) != FLEX_OK);
}

void test_biw(void)
{
	printf("BIW:\n");
	RUN_TEST(test_biw_roundtrip);
	RUN_TEST(test_biw_roundtrip_max);
	RUN_TEST(test_biw_roundtrip_min);
	RUN_TEST(test_biw_bch_correction);
	RUN_TEST(test_biw_bch_correction_2bit);
	RUN_TEST(test_biw_uncorrectable);
	printf("\n");
}
