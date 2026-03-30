#ifndef FLEX_INTERNAL_H
#define FLEX_INTERNAL_H

#include "libflex/types.h"
#include "libflex/bch.h"
#include "libflex/error.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---- Codeword construction ---- */
uint32_t flex_cw_short_addr(uint32_t capcode);
uint32_t flex_cw_long_addr1(uint32_t capcode);
uint32_t flex_cw_long_addr2(uint32_t capcode);
uint32_t flex_cw_vector(flex_msg_type_t type, uint16_t start_word,
                        uint16_t msg_words, int fragment);
uint32_t flex_cw_data(uint32_t data21);

/* ---- Numeric BCD encode/decode ---- */
int flex_numeric_encode(const char *text, size_t len,
                        uint32_t *chunks, int max_chunks);
int flex_numeric_decode(const uint32_t *chunks, int nchunks,
                        char *text, size_t text_cap);

/* ---- 7-bit alpha encode/decode ---- */
int flex_alpha_encode(const char *text, size_t len,
                      uint32_t *chunks, int max_chunks);
int flex_alpha_decode(const uint32_t *chunks, int nchunks,
                      char *text, size_t text_cap);

/* ---- Binary encode/decode ---- */
int flex_binary_encode(const uint8_t *data, size_t len, int bit_width,
                       uint32_t *chunks, int max_chunks);
int flex_binary_decode(const uint32_t *chunks, int nchunks, int bit_width,
                       uint8_t *data, size_t data_cap, size_t *data_len);

/* ---- Interleave ---- */
void flex_interleave(const uint32_t *cws_in, int ncw,
                     uint8_t *bits_out, int *nbits_out);
void flex_deinterleave(const uint8_t *bits_in, int nbits,
                       uint32_t *cws_out, int *ncw_out);

/* ---- Phase separation ---- */
void flex_phase_separate(const uint8_t *symbols, size_t nsymbols,
                         flex_speed_t speed,
                         uint8_t phase_bits[][1024],
                         size_t *phase_bit_counts, int max_phases);
void flex_phase_combine(const uint8_t phase_bits[][1024],
                        size_t bits_per_phase, flex_speed_t speed,
                        uint8_t *symbols_out, size_t *nsymbols_out);

/* ---- Speed helpers ---- */
int      flex_speed_phases(flex_speed_t speed);
int      flex_speed_bps(flex_speed_t speed);
int      flex_speed_is_4fsk(flex_speed_t speed);
uint16_t flex_capcode_to_frame(uint32_t capcode);

/* ---- Bitstream writer ---- */
typedef struct {
	uint8_t *data;
	size_t   cap;
	size_t   byte_pos;
	int      bit_pos;
	size_t   total_bits;
} flex_bs_t;

static inline void bs_init(flex_bs_t *bs, uint8_t *buf, size_t cap)
{
	bs->data = buf;
	bs->cap = cap;
	bs->byte_pos = 0;
	bs->bit_pos = 7;
	bs->total_bits = 0;
	if (cap > 0)
		memset(buf, 0, cap);
}

static inline int bs_write_bit(flex_bs_t *bs, int bit)
{
	if (bs->byte_pos >= bs->cap)
		return -1;
	if (bit)
		bs->data[bs->byte_pos] |= (1u << bs->bit_pos);
	bs->bit_pos--;
	if (bs->bit_pos < 0) {
		bs->bit_pos = 7;
		bs->byte_pos++;
	}
	bs->total_bits++;
	return 0;
}

static inline int bs_write_codeword(flex_bs_t *bs, uint32_t cw)
{
	for (int i = 31; i >= 0; i--) {
		if (bs_write_bit(bs, (cw >> i) & 1) < 0)
			return -1;
	}
	return 0;
}

/* ---- Bitstream reader ---- */
typedef struct {
	const uint8_t *data;
	size_t         len;
	size_t         byte_pos;
	int            bit_pos;
	size_t         total_bits_read;
} flex_br_t;

static inline void br_init(flex_br_t *br, const uint8_t *buf, size_t len)
{
	br->data = buf;
	br->len = len;
	br->byte_pos = 0;
	br->bit_pos = 7;
	br->total_bits_read = 0;
}

static inline int br_read_bit(flex_br_t *br)
{
	if (br->byte_pos >= br->len)
		return -1;
	int bit = (br->data[br->byte_pos] >> br->bit_pos) & 1;
	br->bit_pos--;
	if (br->bit_pos < 0) {
		br->bit_pos = 7;
		br->byte_pos++;
	}
	br->total_bits_read++;
	return bit;
}

static inline int br_read_codeword(flex_br_t *br, uint32_t *cw)
{
	uint32_t val = 0;
	for (int i = 31; i >= 0; i--) {
		int bit = br_read_bit(br);
		if (bit < 0)
			return -1;
		val |= ((uint32_t)bit << i);
	}
	*cw = val;
	return 0;
}

#endif
