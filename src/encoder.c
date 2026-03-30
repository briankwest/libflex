#include "flex_internal.h"
#include "libflex/encoder.h"
#include "libflex/sync.h"
#include "libflex/fiw.h"
#include "libflex/biw.h"
#include <string.h>

void flex_encoder_init(flex_encoder_t *enc, flex_speed_t speed)
{
	memset(enc, 0, sizeof(*enc));
	enc->speed = speed;
}

void flex_encoder_reset(flex_encoder_t *enc)
{
	flex_speed_t speed = enc->speed;
	memset(enc, 0, sizeof(*enc));
	enc->speed = speed;
}

void flex_encoder_set_frame(flex_encoder_t *enc,
                            uint16_t cycle, uint16_t frame)
{
	enc->cycle = cycle;
	enc->frame = frame;
}

flex_err_t flex_encoder_add(flex_encoder_t *enc,
                            uint32_t capcode,
                            flex_addr_type_t addr_type,
                            flex_msg_type_t type,
                            const char *text,
                            const uint8_t *binary_data,
                            size_t binary_len)
{
	if (!enc)
		return FLEX_ERR_PARAM;
	if (enc->count >= FLEX_TX_MAX_MESSAGES)
		return FLEX_ERR_OVERFLOW;

	flex_msg_t *msg = &enc->messages[enc->count];
	memset(msg, 0, sizeof(*msg));
	msg->capcode = capcode;
	msg->addr_type = addr_type;
	msg->type = type;

	if (text && type != FLEX_MSG_BINARY) {
		size_t len = strlen(text);
		if (len >= FLEX_MSG_MAX)
			return FLEX_ERR_OVERFLOW;
		memcpy(msg->text, text, len);
		msg->text_len = len;
	}

	if (binary_data && binary_len > 0 && type == FLEX_MSG_BINARY) {
		if (binary_len > FLEX_MSG_DATA_MAX)
			return FLEX_ERR_OVERFLOW;
		memcpy(msg->data, binary_data, binary_len);
		msg->data_len = binary_len;
	}

	enc->count++;
	return FLEX_OK;
}

typedef struct {
	uint32_t data_cws[80];
	int      data_count;
} msg_data_t;

static int encode_message_data(const flex_msg_t *msg, msg_data_t *md)
{
	md->data_count = 0;

	if (msg->type == FLEX_MSG_TONE_ONLY)
		return 0;

	uint32_t chunks[40];
	int nchunks;

	switch (msg->type) {
	case FLEX_MSG_ALPHA:
		nchunks = flex_alpha_encode(msg->text, msg->text_len, chunks, 40);
		break;
	case FLEX_MSG_NUMERIC:
		nchunks = flex_numeric_encode(msg->text, msg->text_len, chunks, 40);
		break;
	case FLEX_MSG_BINARY:
		nchunks = flex_binary_encode(msg->data, msg->data_len, 8, chunks, 40);
		break;
	default:
		return -1;
	}

	if (nchunks < 0)
		return -1;

	for (int i = 0; i < nchunks && md->data_count < 80; i++)
		md->data_cws[md->data_count++] = flex_cw_data(chunks[i]);

	return md->data_count;
}

/*
 * Total words per phase: 8 codewords/block * 11 blocks = 88
 *
 * Multi-speed support:
 *   1600/2: 1 phase,  256 bits/block, output as 2-FSK bits
 *   3200/2: 2 phases, 512 bits/block, phase bits interleaved A,C,A,C,...
 *   3200/4: 2 phases, 512 bits/block, symbol pairs (A+B),(C+D) alternating
 *   6400/4: 4 phases, 1024 bits/block, symbol pairs (A+B),(C+D) alternating
 *
 * All messages are placed on phase A. Other phases carry idle (zero)
 * codewords which are valid BCH.
 */
#define FRAME_WORDS  (FLEX_CWS_PER_BLOCK * FLEX_BLOCKS_PER_FRAME)

flex_err_t flex_encode(const flex_encoder_t *enc,
                       uint8_t *out, size_t out_cap,
                       size_t *out_len, size_t *out_bits)
{
	if (!enc || !out || !out_len || !out_bits)
		return FLEX_ERR_PARAM;
	if (enc->count == 0)
		return FLEX_ERR_PARAM;

	int nphases = flex_speed_phases(enc->speed);
	int nmsg = (int)enc->count;

	/* --- Step 1: encode each message's data codewords --- */
	msg_data_t msg_data[FLEX_TX_MAX_MESSAGES];
	for (int i = 0; i < nmsg; i++) {
		if (encode_message_data(&enc->messages[i], &msg_data[i]) < 0)
			return FLEX_ERR_BADCHAR;
	}

	/* --- Step 2: build address and vector codewords --- */
	uint32_t addr_cws[FLEX_TX_MAX_MESSAGES * 2];
	int addr_count = 0;

	for (int i = 0; i < nmsg; i++) {
		const flex_msg_t *m = &enc->messages[i];
		if (m->addr_type == FLEX_ADDR_LONG) {
			addr_cws[addr_count++] = flex_cw_long_addr1(m->capcode);
			addr_cws[addr_count++] = flex_cw_long_addr2(m->capcode);
		} else {
			addr_cws[addr_count++] = flex_cw_short_addr(m->capcode);
		}
	}

	/* --- Step 3: build per-phase frame data --- */
	/* phase A gets all messages; other phases are idle (zero = valid BCH) */
	uint32_t phase_frame[FLEX_MAX_PHASES][FRAME_WORDS];
	memset(phase_frame, 0, sizeof(phase_frame));

	/* lay out phase A */
	int addr_start = 1;
	int vect_start = addr_start + addr_count;
	int data_start = vect_start + nmsg;
	int cursor = data_start;

	uint32_t vect_cws[FLEX_TX_MAX_MESSAGES];
	for (int i = 0; i < nmsg; i++) {
		const flex_msg_t *m = &enc->messages[i];
		int msg_start_word = cursor;
		int msg_word_count = msg_data[i].data_count;

		vect_cws[i] = flex_cw_vector(m->type,
		                             (uint16_t)msg_start_word,
		                             (uint16_t)msg_word_count, 0);

		for (int j = 0; j < msg_word_count && cursor < FRAME_WORDS; j++)
			phase_frame[0][cursor++] = msg_data[i].data_cws[j];
	}

	flex_biw_t biw = {
		.priority_addr = 0,
		.addr_field_start = (uint8_t)addr_start,
		.vect_field_start = (uint8_t)vect_start,
		.carry_on = 0
	};
	phase_frame[0][0] = flex_biw_encode(&biw);

	for (int i = 0; i < addr_count && (addr_start + i) < FRAME_WORDS; i++)
		phase_frame[0][addr_start + i] = addr_cws[i];

	for (int i = 0; i < nmsg && (vect_start + i) < FRAME_WORDS; i++)
		phase_frame[0][vect_start + i] = vect_cws[i];

	/* --- Step 4: write the bitstream --- */
	flex_bs_t bs;
	bs_init(&bs, out, out_cap);

	/* Sync1: 64 bits at 1600 bps */
	{
		uint8_t sync1_buf[16];
		size_t sync1_bits = 0;
		flex_err_t err = flex_sync1_build(enc->speed, sync1_buf,
		                                  sizeof(sync1_buf), &sync1_bits);
		if (err != FLEX_OK)
			return err;
		for (size_t i = 0; i < sync1_bits; i++) {
			int bit = (sync1_buf[i / 8] >> (7 - (int)(i % 8))) & 1;
			if (bs_write_bit(&bs, bit) < 0)
				return FLEX_ERR_OVERFLOW;
		}
	}

	/* FIW: 32 bits at 1600 bps */
	{
		flex_fiw_t fiw = { .cycle = enc->cycle, .frame = enc->frame, .repeat = 0 };
		if (bs_write_codeword(&bs, flex_fiw_encode(&fiw)) < 0)
			return FLEX_ERR_OVERFLOW;
	}

	/* Sync2: 32 bits at data rate */
	if (bs_write_codeword(&bs, FLEX_SYNC_MARKER) < 0)
		return FLEX_ERR_OVERFLOW;

	/* Data blocks: 11 blocks, each with nphases × 8 interleaved codewords */
	for (int blk = 0; blk < FLEX_BLOCKS_PER_FRAME; blk++) {
		/* interleave each phase's 8 codewords independently */
		uint8_t phase_ibits[FLEX_MAX_PHASES][256];
		int phase_nbits[FLEX_MAX_PHASES];
		memset(phase_nbits, 0, sizeof(phase_nbits));

		for (int p = 0; p < nphases; p++) {
			uint32_t block_cws[FLEX_CWS_PER_BLOCK];
			for (int w = 0; w < FLEX_CWS_PER_BLOCK; w++)
				block_cws[w] = phase_frame[p][blk * FLEX_CWS_PER_BLOCK + w];
			flex_interleave(block_cws, FLEX_CWS_PER_BLOCK,
			                phase_ibits[p], &phase_nbits[p]);
		}

		/* interleave phases into output stream */
		int bits_per_phase = phase_nbits[0]; /* always 256 */

		if (nphases == 1) {
			/* 1600/2: just write phase A bits */
			for (int i = 0; i < bits_per_phase; i++) {
				if (bs_write_bit(&bs, phase_ibits[0][i]) < 0)
					return FLEX_ERR_OVERFLOW;
			}
		} else if (nphases == 2) {
			/* 3200/2 or 3200/4: alternate phase A and C bits */
			for (int i = 0; i < bits_per_phase; i++) {
				if (bs_write_bit(&bs, phase_ibits[0][i]) < 0)
					return FLEX_ERR_OVERFLOW;
				if (bs_write_bit(&bs, phase_ibits[1][i]) < 0)
					return FLEX_ERR_OVERFLOW;
			}
		} else {
			/* 6400/4: interleave A, B, C, D */
			for (int i = 0; i < bits_per_phase; i++) {
				if (bs_write_bit(&bs, phase_ibits[0][i]) < 0)
					return FLEX_ERR_OVERFLOW;
				if (bs_write_bit(&bs, phase_ibits[1][i]) < 0)
					return FLEX_ERR_OVERFLOW;
				if (bs_write_bit(&bs, phase_ibits[2][i]) < 0)
					return FLEX_ERR_OVERFLOW;
				if (bs_write_bit(&bs, phase_ibits[3][i]) < 0)
					return FLEX_ERR_OVERFLOW;
			}
		}
	}

	*out_bits = bs.total_bits;
	*out_len = bs.byte_pos + (bs.bit_pos < 7 ? 1 : 0);
	return FLEX_OK;
}

flex_err_t flex_encode_single(uint32_t capcode,
                              flex_addr_type_t addr_type,
                              flex_msg_type_t type,
                              flex_speed_t speed,
                              const char *text,
                              const uint8_t *binary_data,
                              size_t binary_len,
                              uint8_t *out, size_t out_cap,
                              size_t *out_len, size_t *out_bits)
{
	flex_encoder_t enc;
	flex_encoder_init(&enc, speed);

	flex_err_t err = flex_encoder_add(&enc, capcode, addr_type, type,
	                                  text, binary_data, binary_len);
	if (err != FLEX_OK)
		return err;

	return flex_encode(&enc, out, out_cap, out_len, out_bits);
}
