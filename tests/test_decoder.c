#include "test.h"
#include <libflex/encoder.h>
#include <libflex/decoder.h>
#include <libflex/types.h>
#include <string.h>

/* --- callback helpers --- */

#define MAX_CB_MSGS 16

static flex_msg_t cb_msgs[MAX_CB_MSGS];
static int cb_count;

static void reset_cb(void)
{
	memset(cb_msgs, 0, sizeof(cb_msgs));
	cb_count = 0;
}

static void msg_callback(const flex_msg_t *msg, void *user)
{
	(void)user;
	if (cb_count < MAX_CB_MSGS)
		cb_msgs[cb_count++] = *msg;
}

/* --- helper: encode then feed to decoder bit-by-bit --- */

static int encode_and_decode(uint32_t capcode, flex_addr_type_t addr_type,
                             flex_msg_type_t type, flex_speed_t speed,
                             const char *text,
                             const uint8_t *bindata, size_t binlen)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(capcode, addr_type, type, speed,
	                                    text, bindata, binlen,
	                                    buf, sizeof(buf), &len, &bits);
	if (err != FLEX_OK)
		return -1;

	reset_cb();
	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);

	/* feed packed bytes */
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	return cb_count;
}

/* --- tests --- */

static void test_decoder_numeric_1600(void)
{
	int n = encode_and_decode(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                          FLEX_SPEED_1600_2, "5551234", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 100000);
	ASSERT_EQ_INT(cb_msgs[0].type, FLEX_MSG_NUMERIC);
	ASSERT_STR_EQ(cb_msgs[0].text, "5551234");
}

static void test_decoder_alpha_1600(void)
{
	int n = encode_and_decode(200000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                          FLEX_SPEED_1600_2, "Hello FLEX", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 200000);
	ASSERT_EQ_INT(cb_msgs[0].type, FLEX_MSG_ALPHA);
	ASSERT_STR_EQ(cb_msgs[0].text, "Hello FLEX");
}

static void test_decoder_tone_1600(void)
{
	int n = encode_and_decode(50000, FLEX_ADDR_SHORT, FLEX_MSG_TONE_ONLY,
	                          FLEX_SPEED_1600_2, NULL, NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 50000);
	ASSERT_EQ_INT(cb_msgs[0].type, FLEX_MSG_TONE_ONLY);
}

static void test_decoder_binary_1600(void)
{
	uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
	int n = encode_and_decode(300000, FLEX_ADDR_SHORT, FLEX_MSG_BINARY,
	                          FLEX_SPEED_1600_2, NULL, data, 4);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 300000);
	ASSERT_EQ_INT(cb_msgs[0].type, FLEX_MSG_BINARY);
	ASSERT(cb_msgs[0].data_len >= 4);
	ASSERT_EQ_INT(cb_msgs[0].data[0], 0xDE);
	ASSERT_EQ_INT(cb_msgs[0].data[1], 0xAD);
	ASSERT_EQ_INT(cb_msgs[0].data[2], 0xBE);
	ASSERT_EQ_INT(cb_msgs[0].data[3], 0xEF);
}

static void test_decoder_numeric_3200_2(void)
{
	int n = encode_and_decode(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                          FLEX_SPEED_3200_2, "999888", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 100000);
	ASSERT_STR_EQ(cb_msgs[0].text, "999888");
	ASSERT_EQ_INT(cb_msgs[0].speed, FLEX_SPEED_3200_2);
}

static void test_decoder_alpha_3200_4(void)
{
	int n = encode_and_decode(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                          FLEX_SPEED_3200_4, "FLEX 3200", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(cb_msgs[0].text, "FLEX 3200");
	ASSERT_EQ_INT(cb_msgs[0].speed, FLEX_SPEED_3200_4);
}

static void test_decoder_alpha_6400_4(void)
{
	int n = encode_and_decode(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                          FLEX_SPEED_6400_4, "FLEX 6400", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(cb_msgs[0].text, "FLEX 6400");
	ASSERT_EQ_INT(cb_msgs[0].speed, FLEX_SPEED_6400_4);
}

static void test_decoder_multi_message(void)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, FLEX_SPEED_1600_2);
	flex_encoder_set_frame(&enc, 3, 50);
	flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_NUMERIC, "111", NULL, 0);
	flex_encoder_add(&enc, 200000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_ALPHA, "Two", NULL, 0);

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;
	ASSERT_EQ_INT(flex_encode(&enc, buf, sizeof(buf), &len, &bits), FLEX_OK);

	reset_cb();
	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	ASSERT_EQ_INT(cb_count, 2);
	ASSERT_EQ_U32(cb_msgs[0].capcode, 100000);
	ASSERT_STR_EQ(cb_msgs[0].text, "111");
	ASSERT_EQ_U32(cb_msgs[1].capcode, 200000);
	ASSERT_STR_EQ(cb_msgs[1].text, "Two");
}

static void test_decoder_fiw_cycle_frame(void)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, FLEX_SPEED_1600_2);
	flex_encoder_set_frame(&enc, 12, 99);
	flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_TONE_ONLY, NULL, NULL, 0);

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;
	flex_encode(&enc, buf, sizeof(buf), &len, &bits);

	reset_cb();
	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	ASSERT_EQ_INT(cb_count, 1);
	ASSERT_EQ_INT(cb_msgs[0].cycle, 12);
	ASSERT_EQ_INT(cb_msgs[0].frame, 99);
}

static void test_decoder_stats(void)
{
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_1600_2, "12345", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);
	reset_cb();
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	ASSERT(dec.stat_frames >= 1);
	ASSERT(dec.stat_codewords > 0);
	ASSERT(dec.stat_messages >= 1);
}

static void test_decoder_reset(void)
{
	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);
	dec.stat_messages = 99;
	dec.state = FLEX_DEC_BLOCK;

	flex_decoder_reset(&dec);
	ASSERT_EQ_INT(dec.state, FLEX_DEC_HUNTING);
	ASSERT_EQ_INT((int)dec.stat_messages, 0);
	ASSERT(dec.callback == msg_callback);
}

static void test_decoder_feed_bits(void)
{
	/* encode, convert to unpacked bits, feed via feed_bits */
	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                   FLEX_SPEED_1600_2, "42", NULL, 0,
	                   buf, sizeof(buf), &len, &bits);

	/* unpack to bit array */
	uint8_t *unpacked = (uint8_t *)buf + FLEX_BITSTREAM_MAX / 2;
	for (size_t i = 0; i < bits; i++)
		unpacked[i] = (buf[i / 8] >> (7 - (i % 8))) & 1;

	reset_cb();
	flex_decoder_t dec;
	flex_decoder_init(&dec, msg_callback, NULL);
	flex_decoder_feed_bits(&dec, unpacked, bits);
	flex_decoder_flush(&dec);

	ASSERT_EQ_INT(cb_count, 1);
	ASSERT_STR_EQ(cb_msgs[0].text, "42");
}

void test_decoder(void)
{
	printf("Decoder:\n");
	RUN_TEST(test_decoder_numeric_1600);
	RUN_TEST(test_decoder_alpha_1600);
	RUN_TEST(test_decoder_tone_1600);
	RUN_TEST(test_decoder_binary_1600);
	RUN_TEST(test_decoder_numeric_3200_2);
	RUN_TEST(test_decoder_alpha_3200_4);
	RUN_TEST(test_decoder_alpha_6400_4);
	RUN_TEST(test_decoder_multi_message);
	RUN_TEST(test_decoder_fiw_cycle_frame);
	RUN_TEST(test_decoder_stats);
	RUN_TEST(test_decoder_reset);
	RUN_TEST(test_decoder_feed_bits);
	printf("\n");
}
