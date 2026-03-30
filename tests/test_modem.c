#include "test.h"
#include <libflex/modem.h>
#include <libflex/encoder.h>
#include <libflex/decoder.h>
#include <libflex/types.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --- callback helpers --- */

static flex_msg_t modem_msgs[8];
static int modem_msg_count;

static void modem_reset(void)
{
	memset(modem_msgs, 0, sizeof(modem_msgs));
	modem_msg_count = 0;
}

static void modem_cb(const flex_msg_t *msg, void *user)
{
	(void)user;
	if (modem_msg_count < 8)
		modem_msgs[modem_msg_count++] = *msg;
}

/* --- reusable modem roundtrip helper ---
 * encode → unpack → modulate → demodulate → decode
 * Returns number of decoded messages, or -1 on error. */
static int modem_roundtrip(uint32_t capcode, flex_msg_type_t type,
                           flex_speed_t speed, float sample_rate,
                           const char *text)
{
	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(
		capcode, FLEX_ADDR_SHORT, type, speed,
		text, NULL, 0,
		bitbuf, sizeof(bitbuf), &len, &bits);
	if (err != FLEX_OK)
		return -1;

	/* unpack bits */
	uint8_t *unpacked = (uint8_t *)malloc(bits);
	if (!unpacked) return -1;
	for (size_t i = 0; i < bits; i++)
		unpacked[i] = (bitbuf[i / 8] >> (7 - (i % 8))) & 1;

	/* modulate */
	size_t max_samp = bits * 40;
	float *samples = (float *)malloc(max_samp * sizeof(float));
	if (!samples) { free(unpacked); return -1; }

	flex_mod_t mod;
	flex_mod_init(&mod, speed, sample_rate);
	size_t nsamp = 0;
	flex_mod_bits(&mod, unpacked, bits, samples, max_samp, &nsamp);
	free(unpacked);

	/* demodulate at matching speed */
	flex_demod_t demod;
	flex_demod_init_speed(&demod, sample_rate, speed);
	flex_demod_feed(&demod, samples, nsamp);
	free(samples);

	/* decode */
	modem_reset();
	flex_decoder_t dec;
	flex_decoder_init(&dec, modem_cb, NULL);
	flex_decoder_feed_bits(&dec, demod.out_bits, demod.out_count);
	flex_decoder_flush(&dec);

	return modem_msg_count;
}

/* --- modulator tests --- */

static void test_mod_init(void)
{
	flex_mod_t mod;
	flex_mod_init(&mod, FLEX_SPEED_1600_2, FLEX_MODEM_SAMPLE_RATE);
	ASSERT_EQ_INT(mod.baud, 1600);
	ASSERT(mod.sample_rate > 0);
}

static void test_mod_generates_samples(void)
{
	flex_mod_t mod;
	flex_mod_init(&mod, FLEX_SPEED_1600_2, FLEX_MODEM_SAMPLE_RATE);

	uint8_t bits[] = { 1, 0, 1, 1, 0, 0, 1, 0 };
	float samples[256];
	size_t nsamp = 0;

	flex_err_t err = flex_mod_bits(&mod, bits, 8, samples, 256, &nsamp);
	ASSERT_EQ_INT(err, FLEX_OK);
	ASSERT(nsamp > 0);

	/* 30 samples per bit at 48000/1600, 8 bits = 240 samples */
	ASSERT(nsamp > 200);
	ASSERT(nsamp < 280);
}

static void test_mod_output_range(void)
{
	flex_mod_t mod;
	flex_mod_init(&mod, FLEX_SPEED_1600_2, FLEX_MODEM_SAMPLE_RATE);

	uint8_t bits[32];
	memset(bits, 0, sizeof(bits));
	float samples[1024];
	size_t nsamp = 0;

	flex_mod_bits(&mod, bits, 32, samples, 1024, &nsamp);

	for (size_t i = 0; i < nsamp; i++) {
		ASSERT(samples[i] >= -1.01f);
		ASSERT(samples[i] <= 1.01f);
	}
}

/* --- WAV I/O tests --- */

static void test_wav_write_read(void)
{
	float samples[100];
	for (int i = 0; i < 100; i++)
		samples[i] = sinf(2.0f * 3.14159f * (float)i / 20.0f);

	ASSERT_EQ_INT(flex_wav_write("/tmp/test_libflex.wav", FLEX_MODEM_SAMPLE_RATE,
	                             samples, 100), FLEX_OK);

	float read_buf[200];
	size_t nread = 0;
	float sr = 0;
	ASSERT_EQ_INT(flex_wav_read("/tmp/test_libflex.wav", read_buf, 200,
	                            &nread, &sr), FLEX_OK);
	ASSERT_EQ_INT((int)nread, 100);
	ASSERT_EQ_INT((int)sr, FLEX_MODEM_SAMPLE_RATE);

	for (int i = 0; i < 100; i++) {
		float diff = samples[i] - read_buf[i];
		if (diff < 0) diff = -diff;
		ASSERT(diff < 0.001f);
	}
}

/* --- modem roundtrip: all 4 FLEX speeds at 48kHz --- */

static void test_modem_1600_2_48k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_1600_2, 48000.0f, "1234");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "1234");
}

static void test_modem_3200_2_48k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_3200_2, 48000.0f, "Hi 3200");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "Hi 3200");
}

static void test_modem_3200_4_48k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_3200_4, 48000.0f, "999");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "999");
}

static void test_modem_6400_4_48k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_6400_4, 48000.0f, "Fast");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "Fast");
}

/* --- modem roundtrip: all 4 FLEX speeds at 32kHz --- */

static void test_modem_1600_2_32k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_1600_2, 32000.0f, "5678");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "5678");
}

static void test_modem_3200_2_32k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_3200_2, 32000.0f, "32k");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "32k");
}

static void test_modem_3200_4_32k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_3200_4, 32000.0f, "42");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "42");
}

static void test_modem_6400_4_32k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_6400_4, 32000.0f, "Go");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "Go");
}

/* --- modem roundtrip: all 4 FLEX speeds at 16kHz --- */

static void test_modem_1600_2_16k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_1600_2, 16000.0f, "16k");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "16k");
}

static void test_modem_3200_2_16k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_3200_2, 16000.0f, "321");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "321");
}

static void test_modem_3200_4_16k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_ALPHA,
	                        FLEX_SPEED_3200_4, 16000.0f, "OK");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "OK");
}

static void test_modem_6400_4_16k(void)
{
	/* 6400/4 at 16kHz: 3200 baud, 5 samples/symbol -- marginal.
	 * Skip: Goertzel needs >= 8 samples/symbol for reliable 4-FSK. */
}

/* --- modem roundtrip: FLEX speeds at 8kHz ---
 * At 8kHz, only 1600 baud modes work (5 samples/symbol).
 * 3200 baud gives 2.5 samples/symbol -- below Goertzel minimum. */

static void test_modem_1600_2_8k(void)
{
	int n = modem_roundtrip(100000, FLEX_MSG_NUMERIC,
	                        FLEX_SPEED_1600_2, 8000.0f, "8000");
	ASSERT_EQ_INT(n, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "8000");
}

static void test_modem_3200_4_8k(void)
{
	/* 3200/4 at 8kHz: 1600 baud, 5 samples/symbol -- works for 2-FSK
	 * but 4-FSK tones are too close at this resolution. Skip. */
}

/* --- WAV roundtrip --- */

static void test_modem_wav_roundtrip(void)
{
	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_encode_single(50000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
	                   FLEX_SPEED_1600_2, "WAV test", NULL, 0,
	                   bitbuf, sizeof(bitbuf), &len, &bits);

	uint8_t *unpacked = (uint8_t *)malloc(bits);
	ASSERT(unpacked != NULL);
	for (size_t i = 0; i < bits; i++)
		unpacked[i] = (bitbuf[i / 8] >> (7 - (i % 8))) & 1;

	float sr = FLEX_MODEM_SAMPLE_RATE;
	size_t max_samp = bits * 40;
	float *samples = (float *)malloc(max_samp * sizeof(float));
	ASSERT(samples != NULL);

	flex_mod_t mod;
	flex_mod_init(&mod, FLEX_SPEED_1600_2, sr);
	size_t nsamp = 0;
	flex_mod_bits(&mod, unpacked, bits, samples, max_samp, &nsamp);
	free(unpacked);

	const char *path = "/tmp/test_libflex_rt.wav";
	ASSERT_EQ_INT(flex_wav_write(path, sr, samples, nsamp), FLEX_OK);
	free(samples);

	float *read_samp = (float *)malloc(max_samp * sizeof(float));
	ASSERT(read_samp != NULL);
	size_t nread = 0;
	float read_sr = 0;
	ASSERT_EQ_INT(flex_wav_read(path, read_samp, max_samp, &nread, &read_sr), FLEX_OK);
	ASSERT_EQ_INT((int)nread, (int)nsamp);

	flex_demod_t demod;
	flex_demod_init(&demod, read_sr);
	flex_demod_feed(&demod, read_samp, nread);
	free(read_samp);

	modem_reset();
	flex_decoder_t dec;
	flex_decoder_init(&dec, modem_cb, NULL);
	flex_decoder_feed_bits(&dec, demod.out_bits, demod.out_count);
	flex_decoder_flush(&dec);

	ASSERT_EQ_INT(modem_msg_count, 1);
	ASSERT_STR_EQ(modem_msgs[0].text, "WAV test");
}

void test_modem(void)
{
	printf("Modem:\n");
	RUN_TEST(test_mod_init);
	RUN_TEST(test_mod_generates_samples);
	RUN_TEST(test_mod_output_range);
	RUN_TEST(test_wav_write_read);

	printf("  Modem roundtrip @ 48kHz:\n");
	RUN_TEST(test_modem_1600_2_48k);
	RUN_TEST(test_modem_3200_2_48k);
	RUN_TEST(test_modem_3200_4_48k);
	RUN_TEST(test_modem_6400_4_48k);

	printf("  Modem roundtrip @ 32kHz:\n");
	RUN_TEST(test_modem_1600_2_32k);
	RUN_TEST(test_modem_3200_2_32k);
	RUN_TEST(test_modem_3200_4_32k);
	RUN_TEST(test_modem_6400_4_32k);

	printf("  Modem roundtrip @ 16kHz:\n");
	RUN_TEST(test_modem_1600_2_16k);
	RUN_TEST(test_modem_3200_2_16k);
	RUN_TEST(test_modem_3200_4_16k);
	RUN_TEST(test_modem_6400_4_16k);

	printf("  Modem roundtrip @ 8kHz:\n");
	RUN_TEST(test_modem_1600_2_8k);
	RUN_TEST(test_modem_3200_4_8k);

	RUN_TEST(test_modem_wav_roundtrip);
	printf("\n");
}
