#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

static int generate(const char *path, uint32_t capcode,
                    flex_msg_type_t type, flex_speed_t speed,
                    float sample_rate, const char *text,
                    const uint8_t *bindata, size_t binlen)
{
	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(capcode, FLEX_ADDR_SHORT, type, speed,
	                                    text, bindata, binlen,
	                                    bitbuf, sizeof(bitbuf), &len, &bits);
	if (err != FLEX_OK) {
		fprintf(stderr, "  encode error: %s\n", flex_strerror(err));
		return -1;
	}

	uint8_t *unpacked = (uint8_t *)malloc(bits);
	if (!unpacked) return -1;
	for (size_t i = 0; i < bits; i++)
		unpacked[i] = (bitbuf[i / 8] >> (7 - (i % 8))) & 1;

	size_t max_samp = bits * 40;
	float *samples = (float *)malloc(max_samp * sizeof(float));
	if (!samples) { free(unpacked); return -1; }

	flex_mod_t mod;
	flex_mod_init(&mod, speed, sample_rate);
	size_t nsamp = 0;
	flex_mod_bits(&mod, unpacked, bits, samples, max_samp, &nsamp);
	free(unpacked);

	err = flex_wav_write(path, sample_rate, samples, nsamp);
	free(samples);

	if (err != FLEX_OK) {
		fprintf(stderr, "  wav write error: %s\n", flex_strerror(err));
		return -1;
	}

	printf("  %s  (%zu samples, %.2fs)\n", path, nsamp, (float)nsamp / sample_rate);
	return 0;
}

static const char *speed_label(flex_speed_t s)
{
	switch (s) {
	case FLEX_SPEED_1600_2: return "1600_2";
	case FLEX_SPEED_3200_2: return "3200_2";
	case FLEX_SPEED_3200_4: return "3200_4";
	case FLEX_SPEED_6400_4: return "6400_4";
	}
	return "unknown";
}

static const char *type_label(flex_msg_type_t t)
{
	switch (t) {
	case FLEX_MSG_NUMERIC:   return "numeric";
	case FLEX_MSG_ALPHA:     return "alpha";
	case FLEX_MSG_BINARY:    return "binary";
	case FLEX_MSG_TONE_ONLY: return "tone";
	default:                 return "unknown";
	}
}

int main(int argc, char **argv)
{
	const char *outdir = "samples";
	if (argc > 1) outdir = argv[1];

	/* sample rates and which FLEX speeds are supported at each */
	struct {
		int sr;
		flex_speed_t speeds[4];
		int nspeeds;
	} rates[] = {
		{ 48000, { FLEX_SPEED_1600_2, FLEX_SPEED_3200_2, FLEX_SPEED_3200_4, FLEX_SPEED_6400_4 }, 4 },
		{ 32000, { FLEX_SPEED_1600_2, FLEX_SPEED_3200_2, FLEX_SPEED_3200_4, FLEX_SPEED_6400_4 }, 4 },
		{ 16000, { FLEX_SPEED_1600_2, FLEX_SPEED_3200_2, FLEX_SPEED_3200_4 }, 3 },
		{  8000, { FLEX_SPEED_1600_2 }, 1 },
	};

	/* message types */
	struct {
		flex_msg_type_t type;
		const char *text;
		const uint8_t *bindata;
		size_t binlen;
	} msgs[] = {
		{ FLEX_MSG_NUMERIC,   "5551234",       NULL, 0 },
		{ FLEX_MSG_ALPHA,     "Hello FLEX!",   NULL, 0 },
		{ FLEX_MSG_BINARY,    NULL,            (const uint8_t *)"\xDE\xAD\xBE\xEF", 4 },
		{ FLEX_MSG_TONE_ONLY, NULL,            NULL, 0 },
	};

	int total = 0, ok = 0;

	for (int r = 0; r < 4; r++) {
		printf("\n=== %d Hz ===\n", rates[r].sr);
		for (int s = 0; s < rates[r].nspeeds; s++) {
			for (int m = 0; m < 4; m++) {
				char path[256];
				snprintf(path, sizeof(path), "%s/%dk_%s_%s.wav",
				         outdir, rates[r].sr / 1000,
				         speed_label(rates[r].speeds[s]),
				         type_label(msgs[m].type));

				total++;
				if (generate(path, 100000 + (uint32_t)(m * 1000),
				             msgs[m].type, rates[r].speeds[s],
				             (float)rates[r].sr,
				             msgs[m].text, msgs[m].bindata,
				             msgs[m].binlen) == 0)
					ok++;
			}
		}
	}

	printf("\n%d/%d WAV files generated\n", ok, total);
	return ok < total ? 1 : 0;
}
