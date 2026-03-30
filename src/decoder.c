#include "flex_internal.h"
#include "libflex/decoder.h"
#include "libflex/sync.h"
#include "libflex/biw.h"
#include <string.h>

void flex_decoder_init(flex_decoder_t *dec,
                       flex_msg_cb_t callback, void *user)
{
	memset(dec, 0, sizeof(*dec));
	dec->callback = callback;
	dec->user = user;
	dec->state = FLEX_DEC_HUNTING;
}

void flex_decoder_reset(flex_decoder_t *dec)
{
	flex_msg_cb_t cb = dec->callback;
	void *user = dec->user;
	memset(dec, 0, sizeof(*dec));
	dec->callback = cb;
	dec->user = user;
	dec->state = FLEX_DEC_HUNTING;
}

/*
 * Decode a short address codeword.
 * Returns capcode or 0 on error.
 */
static uint32_t decode_short_addr(uint32_t cw)
{
	uint32_t data21 = cw & 0x1FFFFFu;
	if (data21 < 32768u)
		return 0;  /* out of short address range */
	return data21 - 32768u;
}

/*
 * Deliver messages from the decoded frame data (phase A).
 * Uses BIW to find address/vector/message boundaries.
 */
static void deliver_messages(flex_decoder_t *dec)
{
	uint32_t *frame = dec->frame_cws[0]; /* phase A */
	int frame_count = dec->frame_cw_count[0];

	if (frame_count < 2)
		return;

	/* decode BIW1 */
	uint32_t biw_cw = frame[0];
	if (flex_bch_correct(&biw_cw) < 0) {
		dec->stat_errors++;
		return;
	}

	flex_biw_t biw;
	if (flex_biw_decode(biw_cw, &biw) != FLEX_OK)
		return;

	int addr_start = biw.addr_field_start;
	int vect_start = biw.vect_field_start;

	if (addr_start >= frame_count || vect_start >= frame_count)
		return;
	if (vect_start <= addr_start)
		return;

	int n_addrs = vect_start - addr_start;

	/* walk address/vector pairs */
	int addr_idx = addr_start;
	for (int v = 0; v < n_addrs && addr_idx < vect_start; v++, addr_idx++) {
		/* correct address codeword */
		uint32_t addr_cw = frame[addr_idx];
		if (flex_bch_correct(&addr_cw) < 0) {
			dec->stat_errors++;
			continue;
		}
		dec->stat_corrected++;

		uint32_t capcode = decode_short_addr(addr_cw);

		/* correct vector codeword */
		int vi = vect_start + v;
		if (vi >= frame_count)
			break;
		uint32_t vect_cw = frame[vi];
		if (flex_bch_correct(&vect_cw) < 0) {
			dec->stat_errors++;
			continue;
		}

		uint32_t vdata = vect_cw & 0x1FFFFFu;
		flex_msg_type_t type = flex_page_type_to_msg_type((vdata >> 4) & 0x7);
		int msg_start = (vdata >> 7) & 0x7F;
		int msg_words = (vdata >> 14) & 0x7F;

		/* build message */
		flex_msg_t msg;
		memset(&msg, 0, sizeof(msg));
		msg.capcode = capcode;
		msg.addr_type = FLEX_ADDR_SHORT;
		msg.type = type;
		msg.speed = dec->speed;
		msg.phase = 0;
		msg.cycle = dec->fiw.cycle;
		msg.frame = dec->fiw.frame;

		if (msg_words > 0 && msg_start < frame_count) {
			/* first word is the message header (frag/cont) — skip it */
			int content_start = msg_start + 1;
			int content_count = msg_words - 1;

			/* collect data chunks from content words */
			uint32_t chunks[88];
			int nchunks = 0;

			for (int w = 0; w < content_count
			     && (content_start + w) < frame_count
			     && nchunks < 88; w++) {
				uint32_t dcw = frame[content_start + w];
				if (flex_bch_correct(&dcw) < 0) {
					dec->stat_errors++;
					continue;
				}
				chunks[nchunks++] = dcw & 0x1FFFFFu;
			}

			/* decode based on type */
			int ret;
			switch (type) {
			case FLEX_MSG_ALPHA:
				/* first content word has 7-bit skip (frag=3) —
				 * shift entire bitstream left by 7 across chunks */
				for (int c = 0; c < nchunks - 1; c++)
					chunks[c] = ((chunks[c] >> 7) |
					             (chunks[c+1] << 14)) & 0x1FFFFFu;
				if (nchunks > 0)
					chunks[nchunks-1] = (chunks[nchunks-1] >> 7) & 0x1FFFFFu;
				ret = flex_alpha_decode(
					chunks, nchunks, msg.text, FLEX_MSG_MAX);
				msg.text_len = ret >= 0 ? (size_t)ret : 0;
				break;
			case FLEX_MSG_NUMERIC:
				ret = flex_numeric_decode(
					chunks, nchunks, msg.text, FLEX_MSG_MAX);
				msg.text_len = ret >= 0 ? (size_t)ret : 0;
				break;
			case FLEX_MSG_BINARY: {
				size_t dlen = 0;
				flex_binary_decode(chunks, nchunks, 8,
				                   msg.data, FLEX_MSG_DATA_MAX, &dlen);
				msg.data_len = dlen;
				break;
			}
			default:
				break;
			}
		}

		/* deliver */
		if (dec->callback)
			dec->callback(&msg, dec->user);
		dec->stat_messages++;
	}
}

/*
 * Process one complete block: deinterleave, BCH correct, store codewords.
 */
static void process_block(flex_decoder_t *dec)
{
	int nphases = flex_speed_phases(dec->speed);
	int blk = dec->block_index;
	int bits_per_phase = 256; /* always 8 cw × 32 bits */

	for (int p = 0; p < nphases; p++) {
		/* extract this phase's bits from interleaved block buffer */
		uint8_t phase_bits[256];
		int pb_count = 0;

		if (nphases == 1) {
			/* 1600/2: all bits are phase A */
			for (int i = 0; i < bits_per_phase && i < dec->block_buf_bits; i++)
				phase_bits[pb_count++] = dec->block_buf[i];
		} else if (nphases == 2) {
			/* 3200: bits alternate A, C */
			for (int i = p; i < dec->block_buf_bits; i += 2) {
				if (pb_count >= bits_per_phase) break;
				phase_bits[pb_count++] = dec->block_buf[i];
			}
		} else {
			/* 6400/4: bits cycle A, B, C, D */
			for (int i = p; i < dec->block_buf_bits; i += 4) {
				if (pb_count >= bits_per_phase) break;
				phase_bits[pb_count++] = dec->block_buf[i];
			}
		}

		/* deinterleave to recover 8 codewords */
		uint32_t cws[FLEX_CWS_PER_BLOCK];
		int ncw = 0;
		flex_deinterleave(phase_bits, pb_count, cws, &ncw);

		/* BCH correct and store in frame */
		int base = blk * FLEX_CWS_PER_BLOCK;
		for (int w = 0; w < ncw && (base + w) < FLEX_DEC_MAX_CW_FRAME; w++) {
			dec->stat_codewords++;

			uint32_t cw = cws[w];
			uint32_t syn = flex_bch_syndrome(cw);

			if (syn != 0) {
				if (flex_bch_correct(&cw) >= 0) {
					dec->stat_corrected++;
				} else {
					dec->stat_errors++;
					/* store as-is; deliver will re-try */
				}
			}

			dec->frame_cws[p][base + w] = cw;
			if (base + w + 1 > dec->frame_cw_count[p])
				dec->frame_cw_count[p] = base + w + 1;
		}
	}
}

/*
 * Process a single bit through the state machine.
 */
static void process_bit(flex_decoder_t *dec, int bit)
{
	switch (dec->state) {

	case FLEX_DEC_HUNTING:
		/* shift bit into 32-bit register, look for inverted sync marker */
		dec->shift_reg = (dec->shift_reg << 1) | (bit & 1);
		if (dec->shift_reg == ~FLEX_SYNC_MARKER) {
			dec->state = FLEX_DEC_SYNC1;
			dec->cw_accum = 0;
			dec->cw_bits = 0;
		}
		break;

	case FLEX_DEC_SYNC1:
		/* accumulate 16-bit C-word (inverted complement of mode).
		 * Sync1 = mode|marker|~mode, transmitted inverted:
		 * ~mode|~marker|mode.  After finding ~marker, next 16 bits
		 * are mode directly (double inversion cancels). */
		dec->cw_accum = (dec->cw_accum << 1) | (bit & 1);
		dec->cw_bits++;
		if (dec->cw_bits == 16) {
			uint16_t mode_word = (uint16_t)(dec->cw_accum & 0xFFFF);
			flex_speed_t speed;
			if (flex_sync_detect_speed(mode_word, &speed) == FLEX_OK) {
				dec->speed = speed;
				dec->state = FLEX_DEC_DOTTING1;
				dec->cw_accum = 0;
				dec->cw_bits = 0;
			} else {
				/* bad mode word, go back to hunting */
				dec->state = FLEX_DEC_HUNTING;
			}
		}
		break;

	case FLEX_DEC_DOTTING1:
		/* skip 16 bits of dotting between Sync1 and FIW */
		dec->cw_bits++;
		if (dec->cw_bits == 16) {
			dec->state = FLEX_DEC_FIW;
			dec->cw_accum = 0;
			dec->cw_bits = 0;
		}
		break;

	case FLEX_DEC_FIW:
		/* accumulate 32-bit FIW codeword (LSB-first: first bit received = bit 0) */
		if (bit & 1)
			dec->cw_accum |= (1u << dec->cw_bits);
		dec->cw_bits++;
		if (dec->cw_bits == 32) {
			flex_fiw_t fiw;
			if (flex_fiw_decode(dec->cw_accum, &fiw) == FLEX_OK) {
				dec->fiw = fiw;
				dec->state = FLEX_DEC_SYNC2;
				dec->cw_accum = 0;
				dec->cw_bits = 0;
				/* compute number of sync2 dotting bits to skip */
				/* sync2 is at the symbol baud rate, not throughput */
			int sym_baud;
			switch (dec->speed) {
			case FLEX_SPEED_1600_2:
			case FLEX_SPEED_3200_4:  sym_baud = 1600; break;
			case FLEX_SPEED_3200_2:
			case FLEX_SPEED_6400_4:  sym_baud = 3200; break;
			default:                 sym_baud = 1600; break;
			}
			dec->skip_bits = sym_baud * 25 / 1000;
			} else {
				dec->state = FLEX_DEC_HUNTING;
			}
		}
		break;

	case FLEX_DEC_SYNC2:
		/* skip sync2 dotting bits */
		dec->cw_bits++;
		if (dec->cw_bits >= dec->skip_bits) {
			dec->state = FLEX_DEC_BLOCK;
			dec->block_index = 0;
			dec->block_buf_bits = 0;
			/* clear frame storage */
			memset(dec->frame_cws, 0, sizeof(dec->frame_cws));
			for (int p = 0; p < FLEX_MAX_PHASES; p++)
				dec->frame_cw_count[p] = 0;
		}
		break;

	case FLEX_DEC_BLOCK: {
		/* accumulate bits for current block */
		int nphases = flex_speed_phases(dec->speed);
		int bits_per_block = 256 * nphases;

		if (dec->block_buf_bits < 1024)
			dec->block_buf[dec->block_buf_bits] = bit & 1;
		dec->block_buf_bits++;

		if (dec->block_buf_bits >= bits_per_block) {
			/* block complete: deinterleave and process */
			process_block(dec);
			dec->block_index++;
			dec->block_buf_bits = 0;

			if (dec->block_index >= FLEX_BLOCKS_PER_FRAME) {
				/* frame complete: deliver messages */
				dec->stat_frames++;
				deliver_messages(dec);
				dec->state = FLEX_DEC_HUNTING;
				dec->shift_reg = 0;
			}
		}
		break;
	}
	}
}

flex_err_t flex_decoder_feed_bits(flex_decoder_t *dec,
                                  const uint8_t *bits, size_t count)
{
	if (!dec || !bits)
		return FLEX_ERR_PARAM;

	for (size_t i = 0; i < count; i++)
		process_bit(dec, bits[i] & 1);

	return FLEX_OK;
}

flex_err_t flex_decoder_feed_bytes(flex_decoder_t *dec,
                                   const uint8_t *data, size_t nbytes)
{
	if (!dec || !data)
		return FLEX_ERR_PARAM;

	for (size_t i = 0; i < nbytes; i++) {
		for (int b = 7; b >= 0; b--)
			process_bit(dec, (data[i] >> b) & 1);
	}

	return FLEX_OK;
}

flex_err_t flex_decoder_feed_symbols(flex_decoder_t *dec,
                                     const uint8_t *symbols, size_t count)
{
	/* 4-FSK symbols: each is 2 bits, MSB first */
	if (!dec || !symbols)
		return FLEX_ERR_PARAM;

	for (size_t i = 0; i < count; i++) {
		process_bit(dec, (symbols[i] >> 1) & 1);
		process_bit(dec, symbols[i] & 1);
	}

	return FLEX_OK;
}

void flex_decoder_flush(flex_decoder_t *dec)
{
	if (!dec)
		return;

	/* if mid-frame, try to deliver whatever we have */
	if (dec->state == FLEX_DEC_BLOCK && dec->block_index > 0) {
		dec->stat_frames++;
		deliver_messages(dec);
	}

	dec->state = FLEX_DEC_HUNTING;
	dec->shift_reg = 0;
}
