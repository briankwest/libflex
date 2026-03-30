#ifndef LIBFLEX_DECODER_H
#define LIBFLEX_DECODER_H

#include "error.h"
#include "types.h"
#include "fiw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flex_msg_cb_t)(const flex_msg_t *msg, void *user);

typedef enum {
	FLEX_DEC_HUNTING = 0,
	FLEX_DEC_SYNC1  = 1,
	FLEX_DEC_FIW    = 2,
	FLEX_DEC_SYNC2  = 3,
	FLEX_DEC_BLOCK  = 4
} flex_dec_state_t;

#define FLEX_DEC_MAX_CW_FRAME  (FLEX_CWS_PER_BLOCK * FLEX_BLOCKS_PER_FRAME)

typedef struct {
	flex_msg_cb_t       callback;
	void               *user;

	/* State machine */
	flex_dec_state_t    state;
	flex_speed_t        speed;

	/* Bit-level accumulation */
	uint32_t            shift_reg;
	uint32_t            cw_accum;
	int                 cw_bits;

	/* Sync1 */
	uint32_t            sync1_buf[4];
	int                 sync1_count;

	/* FIW */
	flex_fiw_t          fiw;

	/* Block processing */
	int                 block_index;
	uint8_t             block_buf[1024];
	int                 block_buf_bits;

	/* Frame-level codewords per phase */
	uint32_t            frame_cws[FLEX_MAX_PHASES][FLEX_DEC_MAX_CW_FRAME];
	int                 frame_cw_count[FLEX_MAX_PHASES];

	/* Statistics */
	uint32_t            stat_codewords;
	uint32_t            stat_corrected;
	uint32_t            stat_errors;
	uint32_t            stat_messages;
	uint32_t            stat_frames;
} flex_decoder_t;

void       flex_decoder_init(flex_decoder_t *dec,
                             flex_msg_cb_t callback, void *user);
void       flex_decoder_reset(flex_decoder_t *dec);

flex_err_t flex_decoder_feed_bits(flex_decoder_t *dec,
                                  const uint8_t *bits, size_t count);
flex_err_t flex_decoder_feed_bytes(flex_decoder_t *dec,
                                   const uint8_t *data, size_t nbytes);
flex_err_t flex_decoder_feed_symbols(flex_decoder_t *dec,
                                     const uint8_t *symbols, size_t count);
void       flex_decoder_flush(flex_decoder_t *dec);

#ifdef __cplusplus
}
#endif

#endif
