#include "test.h"
#include <libflex/bch.h>
#include <libflex/types.h>
#include <string.h>

/* internal functions from interleave.c */
extern void flex_interleave(const uint32_t *cws_in, int ncw,
                            uint8_t *bits_out, int *nbits_out);
extern void flex_deinterleave(const uint8_t *bits_in, int nbits,
                              uint32_t *cws_out, int *ncw_out);

static void test_interleave_roundtrip(void)
{
	uint32_t cws[8];
	for (int i = 0; i < 8; i++)
		cws[i] = flex_codeword_build((uint32_t)(0x100000 + i * 0x11111));

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);
	ASSERT_EQ_INT(nbits, 256);

	uint32_t recovered[8];
	int ncw = 0;
	flex_deinterleave(bits, nbits, recovered, &ncw);
	ASSERT_EQ_INT(ncw, 8);

	for (int i = 0; i < 8; i++)
		ASSERT_EQ_U32(recovered[i], cws[i]);
}

static void test_interleave_known_pattern(void)
{
	/* all-ones codeword should produce all-one bits */
	uint32_t cws[8];
	for (int i = 0; i < 8; i++)
		cws[i] = 0xFFFFFFFFu;

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);
	ASSERT_EQ_INT(nbits, 256);

	for (int i = 0; i < 256; i++)
		ASSERT_EQ_INT(bits[i], 1);
}

static void test_interleave_zeros(void)
{
	uint32_t cws[8];
	memset(cws, 0, sizeof(cws));

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);
	ASSERT_EQ_INT(nbits, 256);

	for (int i = 0; i < 256; i++)
		ASSERT_EQ_INT(bits[i], 0);
}

static void test_interleave_column_order(void)
{
	/* only cw[0] has LSB set -- after LSB-first interleave, bit 0 should be 1,
	 * bits 1-7 should be 0 (those are the other codewords' bit 0) */
	uint32_t cws[8];
	memset(cws, 0, sizeof(cws));
	cws[0] = 0x00000001u;

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);

	ASSERT_EQ_INT(bits[0], 1);  /* cw[0] bit 0 */
	for (int i = 1; i < 256; i++)
		ASSERT_EQ_INT(bits[i], 0);
}

static void test_deinterleave_bch_valid(void)
{
	/* build valid BCH codewords, interleave, deinterleave, verify BCH */
	uint32_t cws[8];
	for (int i = 0; i < 8; i++)
		cws[i] = flex_codeword_build((uint32_t)i * 0x1234);

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);

	uint32_t recovered[8];
	int ncw = 0;
	flex_deinterleave(bits, nbits, recovered, &ncw);

	for (int i = 0; i < 8; i++) {
		ASSERT_EQ_U32(flex_bch_syndrome(recovered[i]), 0);
		ASSERT(flex_parity_check(recovered[i]));
	}
}

static void test_interleave_burst_correction(void)
{
	/* simulate a burst error of 16 consecutive bits in the interleaved stream;
	 * after deinterleaving, each codeword should have at most 2 bit errors,
	 * which BCH can correct */
	uint32_t cws[8];
	for (int i = 0; i < 8; i++)
		cws[i] = flex_codeword_build((uint32_t)(0x0A0000 + i));

	uint8_t bits[256];
	int nbits = 0;
	flex_interleave(cws, 8, bits, &nbits);

	/* corrupt 16 consecutive bits (2 full columns) */
	for (int i = 0; i < 16; i++)
		bits[i] ^= 1;

	uint32_t recovered[8];
	int ncw = 0;
	flex_deinterleave(bits, nbits, recovered, &ncw);

	/* each codeword should have at most 2 errors (16 / 8 = 2) */
	for (int i = 0; i < 8; i++) {
		ASSERT(flex_bch_correct(&recovered[i]) >= 0);
		ASSERT_EQ_U32(recovered[i], cws[i]);
	}
}

void test_interleave(void)
{
	printf("Interleave:\n");
	RUN_TEST(test_interleave_roundtrip);
	RUN_TEST(test_interleave_known_pattern);
	RUN_TEST(test_interleave_zeros);
	RUN_TEST(test_interleave_column_order);
	RUN_TEST(test_deinterleave_bch_valid);
	RUN_TEST(test_interleave_burst_correction);
	printf("\n");
}
