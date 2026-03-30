#ifndef LIBFLEX_TYPES_H
#define LIBFLEX_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Timing constants ---- */
#define FLEX_CYCLES_PER_HOUR     15
#define FLEX_FRAMES_PER_CYCLE    128
#define FLEX_BLOCKS_PER_FRAME    11
#define FLEX_FRAME_DURATION_MS   1875

/* ---- Codeword geometry ---- */
#define FLEX_CW_BITS             32
#define FLEX_BCH_DATA_BITS       21
#define FLEX_BCH_CHECK_BITS      10
#define FLEX_BCH_POLY            0x769u
#define FLEX_CWS_PER_BLOCK       8
#define FLEX_INTERLEAVE_ROWS     32
#define FLEX_INTERLEAVE_COLS     8

/* ---- Speed/modulation ---- */
typedef enum {
	FLEX_SPEED_1600_2 = 0,    /* 1600 bps, 2-FSK */
	FLEX_SPEED_3200_2 = 1,    /* 3200 bps, 2-FSK */
	FLEX_SPEED_3200_4 = 2,    /* 3200 bps, 4-FSK */
	FLEX_SPEED_6400_4 = 3     /* 6400 bps, 4-FSK */
} flex_speed_t;

#define FLEX_MAX_PHASES   4

/* ---- Message types ---- */
typedef enum {
	FLEX_MSG_TONE_ONLY = 0,
	FLEX_MSG_NUMERIC   = 1,
	FLEX_MSG_ALPHA     = 2,
	FLEX_MSG_BINARY    = 3,
	FLEX_MSG_SECURE    = 4
} flex_msg_type_t;

/* ---- Address types ---- */
typedef enum {
	FLEX_ADDR_SHORT = 0,    /* single codeword */
	FLEX_ADDR_LONG  = 1     /* two codewords */
} flex_addr_type_t;

/* ---- Decoded message ---- */
#define FLEX_MSG_MAX       512
#define FLEX_MSG_DATA_MAX  256

typedef struct {
	uint32_t          capcode;
	flex_addr_type_t  addr_type;
	flex_msg_type_t   type;
	flex_speed_t      speed;
	uint8_t           phase;       /* 0=A, 1=B, 2=C, 3=D */
	uint16_t          cycle;       /* 0-14 */
	uint16_t          frame;       /* 0-127 */
	int               is_fragment;
	char              text[FLEX_MSG_MAX];
	size_t            text_len;
	uint8_t           data[FLEX_MSG_DATA_MAX];
	size_t            data_len;
} flex_msg_t;

/* ---- Encoder limits ---- */
#define FLEX_TX_MAX_MESSAGES  32
#define FLEX_BITSTREAM_MAX    16384

/* ---- Speed helpers ---- */
int      flex_speed_phases(flex_speed_t speed);
int      flex_speed_bps(flex_speed_t speed);
int      flex_speed_is_4fsk(flex_speed_t speed);
uint16_t flex_capcode_to_frame(uint32_t capcode);

#ifdef __cplusplus
}
#endif

#endif
