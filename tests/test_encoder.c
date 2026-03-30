#include "test.h"
#include <libflex/encoder.h>
#include <libflex/decoder.h>
#include <libflex/sync.h>
#include <libflex/fiw.h>
#include <libflex/biw.h>
#include <libflex/bch.h>
#include <libflex/types.h>
#include <string.h>

/* internal functions */
extern void flex_deinterleave(const uint8_t *bits_in, int nbits,
                              uint32_t *cws_out, int *ncw_out);

/* helper: extract bits from packed buffer */
static int get_bit(const uint8_t *buf, size_t bit_idx)
{
	return (buf[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
}

/* helper: extract 32-bit codeword starting at bit offset (MSB-first) */
static uint32_t get_codeword(const uint8_t *buf, size_t bit_offset)
{
	uint32_t cw = 0;
	for (int i = 0; i < 32; i++)
		cw |= ((uint32_t)get_bit(buf, bit_offset + (size_t)i) << (31 - i));
	return cw;
}

/* helper: extract 32-bit codeword starting at bit offset (LSB-first) */
static uint32_t get_codeword_lsb(const uint8_t *buf, size_t bit_offset)
{
	uint32_t cw = 0;
	for (int i = 0; i < 32; i++)
		cw |= ((uint32_t)get_bit(buf, bit_offset + (size_t)i) << i);
	return cw;
}

static void test_encoder_single_numeric(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
		FLEX_SPEED_1600_2, "5551234", NULL, 0,
		buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(bits > 0);
	ASSERT(len > 0);
}

static void test_encoder_single_alpha(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		200000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
		FLEX_SPEED_1600_2, "Hello FLEX", NULL, 0,
		buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(bits > 0);
}

static void test_encoder_single_tone(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		50000, FLEX_ADDR_SHORT, FLEX_MSG_TONE_ONLY,
		FLEX_SPEED_1600_2, NULL, NULL, 0,
		buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(bits > 0);
}

static void test_encoder_single_binary(void)
{
	uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		300000, FLEX_ADDR_SHORT, FLEX_MSG_BINARY,
		FLEX_SPEED_1600_2, NULL, data, 4,
		buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(bits > 0);
}

static void test_encoder_sync_present(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_1600_2, "12345", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* Sync1 is inverted: 960-bit preamble + 16-bit inverted bit sync +
	 * 32-bit inverted marker. The inverted sync marker ~0xA6C6AAAA
	 * should be at bit offset 960 + 16 = 976 */
	uint32_t marker = get_codeword(buf, 976);
	ASSERT_EQ_U32(marker, ~FLEX_SYNC_MARKER);
}

static void test_encoder_fiw_valid(void)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, FLEX_SPEED_1600_2);
	flex_encoder_set_frame(&enc, 5, 100);
	flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_NUMERIC, "999", NULL, 0);

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;
	ASSERT_EQ_INT(flex_encode(&enc, buf, sizeof(buf), &len, &bits), FLEX_OK);

	/* FIW is at bit offset 960 + 64 + 16 = 1040, LSB-first */
	uint32_t fiw_cw = get_codeword_lsb(buf, 1040);
	ASSERT_EQ_U32(flex_bch_syndrome(fiw_cw), 0);

	flex_fiw_t fiw;
	ASSERT_EQ_INT(flex_fiw_decode(fiw_cw, &fiw), FLEX_OK);
	ASSERT_EQ_INT(fiw.cycle, 5);
	ASSERT_EQ_INT(fiw.frame, 100);
}

static void test_encoder_biw_valid(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_1600_2, "12345", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* Data starts after preamble(960) + Sync1(64) + dotting(16) + FIW(32)
	 * + sync2_dotting(40) = 1112 bits for 1600/2.
	 * First block: 256 interleaved bits. Deinterleave to get codewords. */
	uint8_t block_bits[256];
	for (int i = 0; i < 256; i++)
		block_bits[i] = get_bit(buf, 1112 + (size_t)i);

	uint32_t cws[8];
	int ncw = 0;
	flex_deinterleave(block_bits, 256, cws, &ncw);
	ASSERT_EQ_INT(ncw, 8);

	/* cw[0] is BIW1 */
	ASSERT_EQ_U32(flex_bch_syndrome(cws[0]), 0);

	flex_biw_t biw;
	ASSERT_EQ_INT(flex_biw_decode(cws[0], &biw), FLEX_OK);
	ASSERT(biw.addr_field_start >= 1);
	ASSERT(biw.vect_field_start > biw.addr_field_start);
}

static void test_encoder_all_codewords_valid(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                   FLEX_SPEED_1600_2, "Test", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* check all 11 blocks * 8 codewords have valid BCH */
	int valid = 0, total = 0;
	for (int blk = 0; blk < 11; blk++) {
		uint8_t block_bits[256];
		for (int i = 0; i < 256; i++)
			block_bits[i] = get_bit(buf, 1112 + (size_t)(blk * 256 + i));

		uint32_t cws[8];
		int ncw = 0;
		flex_deinterleave(block_bits, 256, cws, &ncw);

		for (int w = 0; w < ncw; w++) {
			total++;
			if (flex_bch_syndrome(cws[w]) == 0 && flex_parity_check(cws[w]))
				valid++;
		}
	}

	/* all 88 codewords should be BCH-valid (data or idle=0) */
	ASSERT_EQ_INT(total, 88);
	ASSERT_EQ_INT(valid, total);
}

static void test_encoder_multi_message(void)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, FLEX_SPEED_1600_2);
	flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_NUMERIC, "111", NULL, 0);
	flex_encoder_add(&enc, 200000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_ALPHA, "Hi", NULL, 0);
	flex_encoder_add(&enc, 300000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_TONE_ONLY, NULL, NULL, 0);

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;
	ASSERT_EQ_INT(flex_encode(&enc, buf, sizeof(buf), &len, &bits), FLEX_OK);
	ASSERT(bits > 0);
}

static void test_encoder_expected_size(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_1600_2, "12345", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* Expected: preamble(960) + Sync1(64) + dotting(16) + FIW(32)
	 * + sync2_dotting(40) + 11 blocks * 256 bits
	 * = 960 + 64 + 16 + 32 + 40 + 2816 = 3928 bits */
	ASSERT_EQ_INT((int)bits, 3992);
}

static void test_encoder_long_address(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		5000000, FLEX_ADDR_LONG, FLEX_MSG_NUMERIC,
		FLEX_SPEED_1600_2, "999", NULL, 0,
		buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(bits > 0);
}

static void test_encoder_3200_2_size(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_3200_2, "12345", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* 2 phases: preamble(960) + Sync1(64) + dotting(16) + FIW(32)
	 * + sync2_dotting(80) + 11 * 512 = 960+64+16+32+80+5632 = 6784 */
	ASSERT_EQ_INT((int)bits, 6848);
}

static void test_encoder_3200_4_size(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                   FLEX_SPEED_3200_4, "Test", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* 2 phases at 1600 symbol baud: preamble(960) + Sync1(64) + dotting(16)
	 * + FIW(32) + sync2(40) + 11*512 = 6744 */
	ASSERT_EQ_INT((int)bits, 6808);
}

static void test_encoder_6400_4_size(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                   FLEX_SPEED_6400_4, "Test", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* 4 phases at 3200 symbol baud: preamble(960) + Sync1(64) + dotting(16)
	 * + FIW(32) + sync2(80) + 11*1024 = 12416 */
	ASSERT_EQ_INT((int)bits, 12480);
}

void test_encoder(void)
{
	printf("Encoder:\n");
	RUN_TEST(test_encoder_single_numeric);
	RUN_TEST(test_encoder_single_alpha);
	RUN_TEST(test_encoder_single_tone);
	RUN_TEST(test_encoder_single_binary);
	RUN_TEST(test_encoder_sync_present);
	RUN_TEST(test_encoder_fiw_valid);
	RUN_TEST(test_encoder_biw_valid);
	RUN_TEST(test_encoder_all_codewords_valid);
	RUN_TEST(test_encoder_multi_message);
	RUN_TEST(test_encoder_expected_size);
	RUN_TEST(test_encoder_long_address);
	RUN_TEST(test_encoder_3200_2_size);
	RUN_TEST(test_encoder_3200_4_size);
	RUN_TEST(test_encoder_6400_4_size);
	printf("\n");
}
