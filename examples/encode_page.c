#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libflex/flex.h>

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <capcode> <n|a|t> [speed] [message]\n"
		"  capcode: pager address (decimal)\n"
		"  n = numeric, a = alphanumeric, t = tone-only\n"
		"  speed:  1600 (default), 3200, 3200-4, 6400\n"
		"  message: text (omit for tone-only)\n"
		"Output: raw FLEX frame bitstream on stdout\n",
		prog);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	uint32_t capcode = (uint32_t)strtoul(argv[1], NULL, 10);
	char type_ch = argv[2][0];

	flex_msg_type_t type;
	const char *text = NULL;
	flex_speed_t speed = FLEX_SPEED_1600_2;

	switch (type_ch) {
	case 'n': type = FLEX_MSG_NUMERIC; break;
	case 'a': type = FLEX_MSG_ALPHA; break;
	case 't': type = FLEX_MSG_TONE_ONLY; break;
	default:
		usage(argv[0]);
		return 1;
	}

	int arg_idx = 3;

	/* optional speed argument */
	if (arg_idx < argc) {
		if (strcmp(argv[arg_idx], "3200") == 0) {
			speed = FLEX_SPEED_3200_2;
			arg_idx++;
		} else if (strcmp(argv[arg_idx], "3200-4") == 0) {
			speed = FLEX_SPEED_3200_4;
			arg_idx++;
		} else if (strcmp(argv[arg_idx], "6400") == 0) {
			speed = FLEX_SPEED_6400_4;
			arg_idx++;
		} else if (strcmp(argv[arg_idx], "1600") == 0) {
			speed = FLEX_SPEED_1600_2;
			arg_idx++;
		}
	}

	/* message text */
	if (arg_idx < argc && type != FLEX_MSG_TONE_ONLY)
		text = argv[arg_idx];

	uint8_t buf[FLEX_BITSTREAM_MAX];
	size_t len, bits;

	flex_err_t err = flex_encode_single(capcode, FLEX_ADDR_SHORT, type, speed,
	                                    text, NULL, 0,
	                                    buf, sizeof(buf), &len, &bits);
	if (err != FLEX_OK) {
		fprintf(stderr, "encode error: %s\n", flex_strerror(err));
		return 1;
	}

	fwrite(buf, 1, len, stdout);
	fprintf(stderr, "encoded %zu bits (%zu bytes) at %d bps\n",
	        bits, len, flex_speed_bps(speed));

	return 0;
}
