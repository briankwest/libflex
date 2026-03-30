/*
 * gen_baseband - Generate FLEX baseband WAV files using the
 *                library's built-in flex_baseband() function.
 *
 * The encoder (flex_encode_single) now produces multimon-ng-compatible
 * frames with proper preamble, inverted sync, dotting, LSB-first FIW,
 * and LSB-first interleaved data.  flex_baseband() converts that
 * bitstream to NRZ/4-level samples.
 *
 * Generates the full matrix: 4 modes × 4 sample rates.
 * Output: 16-bit mono PCM WAV.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <libflex/flex.h>

#define MIN_SPB  5

typedef struct {
	flex_speed_t speed;
	int          baud;
	const char  *label;
} mode_entry_t;

static const mode_entry_t modes[] = {
	{ FLEX_SPEED_1600_2, 1600, "1600_2" },
	{ FLEX_SPEED_3200_4, 1600, "1600_4" },  /* 1600 baud, 4-FSK */
	{ FLEX_SPEED_3200_2, 3200, "3200_2" },
	{ FLEX_SPEED_6400_4, 3200, "3200_4" },  /* 3200 baud, 4-FSK */
};
#define NUM_MODES 4

static const uint32_t srates[] = { 8000, 16000, 32000, 48000 };

/* ----------------------------------------------------------------- */

#define MAX_SAMPLES (2 * 1024 * 1024)
static float audio[MAX_SAMPLES];

static int generate(const mode_entry_t *mode, uint32_t sr, const char *dir)
{
	/* encode to bitstream */
	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t len = 0, bits = 0;

	flex_err_t err = flex_encode_single(101000, FLEX_ADDR_SHORT,
	                                    FLEX_MSG_ALPHA, mode->speed,
	                                    "Hello FLEX!", NULL, 0,
	                                    bitbuf, sizeof(bitbuf),
	                                    &len, &bits);
	if (err != FLEX_OK) {
		fprintf(stderr, "  encode error: %s\n", flex_strerror(err));
		return -1;
	}

	/* 0.25s silence + baseband + 0.25s silence */
	size_t pad = (size_t)(sr * 0.25f);
	memset(audio, 0, pad * sizeof(float));

	size_t sig_len = 0;
	err = flex_baseband(bitbuf, bits, mode->speed, (float)sr,
	                    audio + pad, MAX_SAMPLES - pad * 2, &sig_len);
	if (err != FLEX_OK) {
		fprintf(stderr, "  baseband error: %s\n", flex_strerror(err));
		return -1;
	}

	size_t total = pad + sig_len + pad;
	memset(audio + pad + sig_len, 0, pad * sizeof(float));

	char path[256];
	snprintf(path, sizeof(path), "%s/flex_%s_%uhz.wav",
	         dir, mode->label, sr);

	err = flex_wav_write(path, (float)sr, audio, total);
	if (err != FLEX_OK) {
		fprintf(stderr, "  wav write error: %s\n", flex_strerror(err));
		return -1;
	}

	printf("  %-50s  %6zu samples  %.2fs\n",
	       path, total, (double)total / sr);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "baseband";
	mkdir(dir, 0755);

	int ok = 0, total = 0, skip = 0;

	printf("Generating FLEX baseband WAV files in %s/\n\n", dir);

	for (int s = 0; s < 4; s++) {
		uint32_t sr = srates[s];
		printf("=== %u Hz ===\n", sr);

		for (int m = 0; m < NUM_MODES; m++) {
			if (sr / (uint32_t)modes[m].baud < MIN_SPB) {
				printf("  skip %s at %u Hz (%.1f spb)\n",
				       modes[m].label, sr,
				       (double)sr / modes[m].baud);
				skip++;
				continue;
			}

			total++;
			if (generate(&modes[m], sr, dir) == 0)
				ok++;
		}
		printf("\n");
	}

	printf("%d/%d files written, %d skipped\n", ok, total, skip);
	return ok == total ? 0 : 1;
}
