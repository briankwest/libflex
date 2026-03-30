#include "test.h"
#include <libflex/encoder.h>
#include <libflex/decoder.h>
#include <libflex/types.h>
#include <string.h>

/* --- callback helpers --- */

#define MAX_CB_MSGS 16

static flex_msg_t rt_msgs[MAX_CB_MSGS];
static int rt_count;

static void rt_reset(void)
{
	memset(rt_msgs, 0, sizeof(rt_msgs));
	rt_count = 0;
}

static void rt_callback(const flex_msg_t *msg, void *user)
{
	(void)user;
	if (rt_count < MAX_CB_MSGS)
		rt_msgs[rt_count++] = *msg;
}

static int roundtrip(uint32_t capcode, flex_addr_type_t addr_type,
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

	rt_reset();
	flex_decoder_t dec;
	flex_decoder_init(&dec, rt_callback, NULL);
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	return rt_count;
}

/* --- roundtrip tests for all types at 1600/2 --- */

static void test_rt_numeric_1600(void)
{
	int n = roundtrip(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                  FLEX_SPEED_1600_2, "5551234", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(rt_msgs[0].capcode, 100000);
	ASSERT_STR_EQ(rt_msgs[0].text, "5551234");
}

static void test_rt_alpha_1600(void)
{
	int n = roundtrip(200000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                  FLEX_SPEED_1600_2, "Hello FLEX paging!", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(rt_msgs[0].text, "Hello FLEX paging!");
}

static void test_rt_tone_1600(void)
{
	int n = roundtrip(50000, FLEX_ADDR_SHORT, FLEX_MSG_TONE_ONLY,
	                  FLEX_SPEED_1600_2, NULL, NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_INT(rt_msgs[0].type, FLEX_MSG_TONE_ONLY);
}

static void test_rt_binary_1600(void)
{
	uint8_t data[] = { 0xCA, 0xFE, 0xBA, 0xBE };
	int n = roundtrip(300000, FLEX_ADDR_SHORT, FLEX_MSG_BINARY,
	                  FLEX_SPEED_1600_2, NULL, data, 4);
	ASSERT_EQ_INT(n, 1);
	ASSERT(rt_msgs[0].data_len >= 4);
	ASSERT_EQ_INT(rt_msgs[0].data[0], 0xCA);
	ASSERT_EQ_INT(rt_msgs[0].data[1], 0xFE);
	ASSERT_EQ_INT(rt_msgs[0].data[2], 0xBA);
	ASSERT_EQ_INT(rt_msgs[0].data[3], 0xBE);
}

/* --- roundtrip at 3200/2 --- */

static void test_rt_alpha_3200_2(void)
{
	int n = roundtrip(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                  FLEX_SPEED_3200_2, "FLEX at 3200/2", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(rt_msgs[0].text, "FLEX at 3200/2");
	ASSERT_EQ_INT(rt_msgs[0].speed, FLEX_SPEED_3200_2);
}

/* --- roundtrip at 3200/4 --- */

static void test_rt_numeric_3200_4(void)
{
	int n = roundtrip(150000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                  FLEX_SPEED_3200_4, "8675309", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(rt_msgs[0].text, "8675309");
	ASSERT_EQ_INT(rt_msgs[0].speed, FLEX_SPEED_3200_4);
}

/* --- roundtrip at 6400/4 --- */

static void test_rt_alpha_6400_4(void)
{
	int n = roundtrip(400000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                  FLEX_SPEED_6400_4, "Full speed FLEX!", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(rt_msgs[0].text, "Full speed FLEX!");
	ASSERT_EQ_INT(rt_msgs[0].speed, FLEX_SPEED_6400_4);
}

/* --- multi-message roundtrip --- */

static void test_rt_multi_message(void)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, FLEX_SPEED_1600_2);
	flex_encoder_set_frame(&enc, 7, 64);
	flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_NUMERIC, "111", NULL, 0);
	flex_encoder_add(&enc, 200000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_ALPHA, "Two", NULL, 0);
	flex_encoder_add(&enc, 300000, FLEX_ADDR_SHORT,
	                 FLEX_MSG_TONE_ONLY, NULL, NULL, 0);

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;
	ASSERT_EQ_INT(flex_encode(&enc, buf, sizeof(buf), &len, &bits), FLEX_OK);

	rt_reset();
	flex_decoder_t dec;
	flex_decoder_init(&dec, rt_callback, NULL);
	flex_decoder_feed_bytes(&dec, buf, len);
	flex_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 3);
	ASSERT_STR_EQ(rt_msgs[0].text, "111");
	ASSERT_STR_EQ(rt_msgs[1].text, "Two");
	ASSERT_EQ_INT(rt_msgs[2].type, FLEX_MSG_TONE_ONLY);
}

/* --- edge cases --- */

static void test_rt_address_zero(void)
{
	int n = roundtrip(0, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
	                  FLEX_SPEED_1600_2, "999", NULL, 0);
	ASSERT_EQ_INT(n, 1);
	ASSERT_EQ_U32(rt_msgs[0].capcode, 0);
}

static void test_rt_all_speeds_tone(void)
{
	flex_speed_t speeds[] = {
		FLEX_SPEED_1600_2, FLEX_SPEED_3200_2,
		FLEX_SPEED_3200_4, FLEX_SPEED_6400_4
	};
	for (int i = 0; i < 4; i++) {
		int n = roundtrip(100000, FLEX_ADDR_SHORT, FLEX_MSG_TONE_ONLY,
		                  speeds[i], NULL, NULL, 0);
		ASSERT_EQ_INT(n, 1);
		ASSERT_EQ_INT(rt_msgs[0].speed, speeds[i]);
	}
}

void test_roundtrip(void)
{
	printf("Roundtrip:\n");
	RUN_TEST(test_rt_numeric_1600);
	RUN_TEST(test_rt_alpha_1600);
	RUN_TEST(test_rt_tone_1600);
	RUN_TEST(test_rt_binary_1600);
	RUN_TEST(test_rt_alpha_3200_2);
	RUN_TEST(test_rt_numeric_3200_4);
	RUN_TEST(test_rt_alpha_6400_4);
	RUN_TEST(test_rt_multi_message);
	RUN_TEST(test_rt_address_zero);
	RUN_TEST(test_rt_all_speeds_tone);
	printf("\n");
}
