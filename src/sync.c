#include "libflex/sync.h"
#include "flex_internal.h"

/*
 * Mode word table: maps 16-bit mode words to speed settings.
 * These constants come from the FLEX protocol specification
 * (confirmed via multimon-ng and PDW decoder sources).
 */
static const struct {
	uint16_t     mode_word;
	flex_speed_t speed;
} mode_table[] = {
	{ FLEX_MODE_1600_2, FLEX_SPEED_1600_2 },
	{ FLEX_MODE_1600_4, FLEX_SPEED_3200_4 },
	{ FLEX_MODE_3200_2, FLEX_SPEED_3200_2 },
	{ FLEX_MODE_3200_4, FLEX_SPEED_6400_4 },
};

#define MODE_TABLE_SIZE  (sizeof(mode_table) / sizeof(mode_table[0]))
#define MAX_HAMMING_DIST 3  /* tolerate up to 3 bit errors in mode word */

int flex_hamming16(uint16_t a, uint16_t b)
{
	uint16_t x = a ^ b;
	int count = 0;
	while (x) {
		count += x & 1;
		x >>= 1;
	}
	return count;
}

flex_err_t flex_sync_detect_speed(uint16_t mode_word, flex_speed_t *speed)
{
	if (!speed)
		return FLEX_ERR_PARAM;

	/* exact match first */
	for (unsigned i = 0; i < MODE_TABLE_SIZE; i++) {
		if (mode_word == mode_table[i].mode_word) {
			*speed = mode_table[i].speed;
			return FLEX_OK;
		}
	}

	/* fuzzy match with Hamming distance */
	int best_dist = 17;
	int best_idx = -1;
	for (unsigned i = 0; i < MODE_TABLE_SIZE; i++) {
		int d = flex_hamming16(mode_word, mode_table[i].mode_word);
		if (d < best_dist) {
			best_dist = d;
			best_idx = (int)i;
		}
	}

	if (best_idx >= 0 && best_dist <= MAX_HAMMING_DIST) {
		*speed = mode_table[best_idx].speed;
		return FLEX_OK;
	}

	return FLEX_ERR_SYNC;
}

/*
 * Build Sync1 (Frame Sync): A-word + B-word + C-word
 *   A-word: 16-bit mode/sync code
 *   B-word: 32-bit sync marker (0xA6C6AAAA)
 *   C-word: 16-bit complement of A-word
 * Total: 64 bits = 8 bytes, always at 1600 bps 2-FSK.
 */
flex_err_t flex_sync1_build(flex_speed_t speed,
                            uint8_t *out, size_t out_cap,
                            size_t *out_bits)
{
	if (!out || !out_bits)
		return FLEX_ERR_PARAM;

	uint16_t mode_word = 0;
	for (unsigned i = 0; i < MODE_TABLE_SIZE; i++) {
		if (mode_table[i].speed == speed) {
			mode_word = mode_table[i].mode_word;
			break;
		}
	}
	if (mode_word == 0)
		return FLEX_ERR_SPEED;

	if (out_cap < 8)
		return FLEX_ERR_OVERFLOW;

	flex_bs_t bs;
	bs_init(&bs, out, out_cap);

	/* A-word: mode/sync code (16 bits, MSB first) */
	for (int i = 15; i >= 0; i--)
		bs_write_bit(&bs, (mode_word >> i) & 1);

	/* B-word: sync marker (32 bits, MSB first) */
	bs_write_codeword(&bs, FLEX_SYNC_MARKER);

	/* C-word: complement of A-word (16 bits, MSB first) */
	uint16_t complement = mode_word ^ 0xFFFFu;
	for (int i = 15; i >= 0; i--)
		bs_write_bit(&bs, (complement >> i) & 1);

	*out_bits = bs.total_bits;
	return FLEX_OK;
}

/*
 * Build Sync2: sync marker at data rate.
 * 32 bits = 4 bytes.
 */
flex_err_t flex_sync2_build(uint8_t *out, size_t out_cap,
                            size_t *out_bits)
{
	if (!out || !out_bits)
		return FLEX_ERR_PARAM;
	if (out_cap < 4)
		return FLEX_ERR_OVERFLOW;

	flex_bs_t bs;
	bs_init(&bs, out, out_cap);
	bs_write_codeword(&bs, FLEX_SYNC_MARKER);

	*out_bits = bs.total_bits;
	return FLEX_OK;
}
