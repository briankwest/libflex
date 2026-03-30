#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <capcode> <n|a|t> [speed] [message] -o <file.wav>\n"
		"  capcode: pager address (decimal)\n"
		"  n = numeric, a = alphanumeric, t = tone-only\n"
		"  speed:  1600 (default), 3200, 3200-4, 6400\n"
		"  -o:     output WAV file path\n",
		prog);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		usage(argv[0]);
		return 1;
	}

	uint32_t capcode = (uint32_t)strtoul(argv[1], NULL, 10);
	char type_ch = argv[2][0];

	flex_msg_type_t type;
	const char *text = NULL;
	const char *wav_path = NULL;
	flex_speed_t speed = FLEX_SPEED_1600_2;

	switch (type_ch) {
	case 'n': type = FLEX_MSG_NUMERIC; break;
	case 'a': type = FLEX_MSG_ALPHA; break;
	case 't': type = FLEX_MSG_TONE_ONLY; break;
	default:
		usage(argv[0]);
		return 1;
	}

	/* parse remaining args */
	for (int i = 3; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			wav_path = argv[++i];
		} else if (strcmp(argv[i], "1600") == 0) {
			speed = FLEX_SPEED_1600_2;
		} else if (strcmp(argv[i], "3200") == 0) {
			speed = FLEX_SPEED_3200_2;
		} else if (strcmp(argv[i], "3200-4") == 0) {
			speed = FLEX_SPEED_3200_4;
		} else if (strcmp(argv[i], "6400") == 0) {
			speed = FLEX_SPEED_6400_4;
		} else if (type != FLEX_MSG_TONE_ONLY && !text) {
			text = argv[i];
		}
	}

	if (!wav_path) {
		fprintf(stderr, "error: -o <file.wav> is required\n");
		usage(argv[0]);
		return 1;
	}

	/* Step 1: encode to bitstream */
	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t len, bits;

	flex_err_t err = flex_encode_single(capcode, FLEX_ADDR_SHORT, type, speed,
	                                    text, NULL, 0,
	                                    bitbuf, sizeof(bitbuf), &len, &bits);
	if (err != FLEX_OK) {
		fprintf(stderr, "encode error: %s\n", flex_strerror(err));
		return 1;
	}

	/* Step 2: unpack to bit array */
	uint8_t *unpacked = (uint8_t *)malloc(bits);
	if (!unpacked) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}
	for (size_t i = 0; i < bits; i++)
		unpacked[i] = (bitbuf[i / 8] >> (7 - (i % 8))) & 1;

	/* Step 3: modulate to audio */
	float sample_rate = FLEX_MODEM_SAMPLE_RATE;
	size_t max_samples = (bits * (size_t)(sample_rate / 1600.0f + 1)) + 1024;
	float *samples = (float *)malloc(max_samples * sizeof(float));
	if (!samples) {
		free(unpacked);
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	flex_mod_t mod;
	flex_mod_init(&mod, speed, sample_rate);

	size_t nsamples;
	err = flex_mod_bits(&mod, unpacked, bits, samples, max_samples, &nsamples);
	free(unpacked);

	if (err != FLEX_OK) {
		free(samples);
		fprintf(stderr, "modulate error: %s\n", flex_strerror(err));
		return 1;
	}

	/* Step 4: write WAV */
	err = flex_wav_write(wav_path, sample_rate, samples, nsamples);
	free(samples);

	if (err != FLEX_OK) {
		fprintf(stderr, "WAV write error: %s\n", flex_strerror(err));
		return 1;
	}

	fprintf(stderr, "wrote %s: %zu samples (%.2f sec) at %d Hz, %d bps\n",
	        wav_path, nsamples, (float)nsamples / sample_rate,
	        (int)sample_rate, flex_speed_bps(speed));

	return 0;
}
