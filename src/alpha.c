#include "flex_internal.h"

/*
 * FLEX alphanumeric encoding: 3 x 7-bit characters packed per 21-bit
 * data word.  Characters are extracted from the data word as:
 *   char 0 = bits 0-6   (data & 0x7F)
 *   char 1 = bits 7-13  ((data >> 7) & 0x7F)
 *   char 2 = bits 14-20 ((data >> 14) & 0x7F)
 *
 * This differs from POCSAG's LSB-first 20-bit chunk packing.
 * In FLEX, the packing is native to the 21-bit BCH data field.
 */

int flex_alpha_encode(const char *text, size_t len,
                      uint32_t *chunks, int max_chunks)
{
	if (!text || !chunks)
		return -1;

	int nchunks = 0;
	uint32_t chunk = 0;
	int chars_in_chunk = 0;

	for (size_t i = 0; i < len; i++) {
		uint8_t ch = (uint8_t)text[i] & 0x7F;
		chunk |= ((uint32_t)ch << (chars_in_chunk * 7));
		chars_in_chunk++;

		if (chars_in_chunk == 3) {
			if (nchunks >= max_chunks)
				return -1;
			chunks[nchunks++] = chunk;
			chunk = 0;
			chars_in_chunk = 0;
		}
	}

	/* flush partial chunk (remaining chars padded with NUL) */
	if (chars_in_chunk > 0) {
		if (nchunks >= max_chunks)
			return -1;
		chunks[nchunks++] = chunk;
	}

	return nchunks;
}

int flex_alpha_decode(const uint32_t *chunks, int nchunks,
                      char *text, size_t text_cap)
{
	if (!chunks || !text || text_cap == 0)
		return -1;

	size_t pos = 0;

	for (int c = 0; c < nchunks; c++) {
		for (int k = 0; k < 3; k++) {
			uint8_t ch = (chunks[c] >> (k * 7)) & 0x7F;

			if (ch == 0x00 || ch == 0x03)
				goto done;

			if (pos + 1 >= text_cap)
				goto done;

			text[pos++] = (char)ch;
		}
	}

done:
	text[pos] = '\0';
	return (int)pos;
}
