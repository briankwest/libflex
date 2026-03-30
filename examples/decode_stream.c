#include <stdio.h>
#include <libflex/flex.h>

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

int main(void)
{
	flex_decoder_t dec;
	flex_decoder_init(&dec, on_message, NULL);

	uint8_t buf[4096];
	size_t n;

	while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
		flex_decoder_feed_bytes(&dec, buf, n);

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
