#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

static void on_message(const flex_msg_t *msg, void *user)
{
	(void)user;

	const char *type;
	switch (msg->type) {
	case FLEX_MSG_NUMERIC:   type = "NUM";    break;
	case FLEX_MSG_ALPHA:     type = "ALPHA";  break;
	case FLEX_MSG_BINARY:    type = "BIN";    break;
	case FLEX_MSG_TONE_ONLY: type = "TONE";   break;
	default:                 type = "???";    break;
	}

	printf("[%s] capcode=%u cycle=%u frame=%u speed=%d",
	       type, (unsigned)msg->capcode,
	       (unsigned)msg->cycle, (unsigned)msg->frame,
	       flex_speed_bps(msg->speed));

	if (msg->text_len > 0)
		printf(" msg=\"%s\"", msg->text);

	if (msg->data_len > 0) {
		printf(" data=[");
		for (size_t i = 0; i < msg->data_len; i++)
			printf("%s%02X", i ? " " : "", msg->data[i]);
		printf("]");
	}

	printf("\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <file.wav> [speed]\n"
		        "  speed: 1600 (default), 3200, 3200-4, 6400\n",
		        argv[0]);
		return 1;
	}

	/* parse optional speed */
	flex_speed_t speed = FLEX_SPEED_1600_2;
	if (argc > 2) {
		if (strcmp(argv[2], "3200") == 0)
			speed = FLEX_SPEED_3200_2;
		else if (strcmp(argv[2], "3200-4") == 0)
			speed = FLEX_SPEED_3200_4;
		else if (strcmp(argv[2], "6400") == 0)
			speed = FLEX_SPEED_6400_4;
	}

	/* Step 1: read WAV file */
	size_t max_samples = 48000 * 10;
	float *samples = (float *)malloc(max_samples * sizeof(float));
	if (!samples) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	size_t nsamples;
	float sample_rate;
	flex_err_t err = flex_wav_read(argv[1], samples, max_samples,
	                               &nsamples, &sample_rate);
	if (err != FLEX_OK) {
		free(samples);
		fprintf(stderr, "error reading WAV: %s\n", flex_strerror(err));
		return 1;
	}

	fprintf(stderr, "read %s: %zu samples at %.0f Hz (%.2f sec)\n",
	        argv[1], nsamples, sample_rate, (float)nsamples / sample_rate);

	/* Step 2: demodulate */
	flex_demod_t demod;
	flex_demod_init_speed(&demod, sample_rate, speed);
	flex_demod_feed(&demod, samples, nsamples);
	free(samples);

	fprintf(stderr, "demodulated %zu bits\n", demod.out_count);

	/* Step 3: decode */
	flex_decoder_t dec;
	flex_decoder_init(&dec, on_message, NULL);
	flex_decoder_feed_bits(&dec, demod.out_bits, demod.out_count);
	flex_decoder_flush(&dec);

	fprintf(stderr,
		"frames=%u codewords=%u corrected=%u errors=%u messages=%u\n",
		(unsigned)dec.stat_frames,
		(unsigned)dec.stat_codewords,
		(unsigned)dec.stat_corrected,
		(unsigned)dec.stat_errors,
		(unsigned)dec.stat_messages);

	return 0;
}
