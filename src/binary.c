#include "flex_internal.h"

/*
 * FLEX binary message encoding: arbitrary binary data packed into
 * 21-bit codeword data fields.  Each chunk holds floor(21 / bit_width)
 * items of the given bit width.
 *
 * For general use, bit_width=8 packs 2 bytes per chunk (16 bits used,
 * 5 bits unused).  To maximize throughput, the full 21 bits can be
 * used with bit_width=7 (3 items) or bit_width=1 (21 items).
 */

int flex_binary_encode(const uint8_t *data, size_t len, int bit_width,
                       uint32_t *chunks, int max_chunks)
{
	if (!data || !chunks || bit_width < 1 || bit_width > 16)
		return -1;

	int nchunks = 0;
	uint32_t chunk = 0;
	int bits_used = 0;
	int items_per_chunk = 21 / bit_width;
	int items_in_chunk = 0;

	for (size_t i = 0; i < len; i++) {
		uint32_t val = data[i] & ((1u << bit_width) - 1);
		chunk |= (val << bits_used);
		bits_used += bit_width;
		items_in_chunk++;

		if (items_in_chunk >= items_per_chunk) {
			if (nchunks >= max_chunks)
				return -1;
			chunks[nchunks++] = chunk & 0x1FFFFFu;
			chunk = 0;
			bits_used = 0;
			items_in_chunk = 0;
		}
	}

	/* flush partial chunk */
	if (items_in_chunk > 0) {
		if (nchunks >= max_chunks)
			return -1;
		chunks[nchunks++] = chunk & 0x1FFFFFu;
	}

	return nchunks;
}

int flex_binary_decode(const uint32_t *chunks, int nchunks, int bit_width,
                       uint8_t *data, size_t data_cap, size_t *data_len)
{
	if (!chunks || !data || !data_len || bit_width < 1 || bit_width > 16)
		return -1;

	int items_per_chunk = 21 / bit_width;
	uint32_t mask = (1u << bit_width) - 1;
	size_t pos = 0;

	for (int c = 0; c < nchunks; c++) {
		int bits_read = 0;
		for (int k = 0; k < items_per_chunk; k++) {
			if (pos >= data_cap)
				goto done;
			data[pos++] = (chunks[c] >> bits_read) & mask;
			bits_read += bit_width;
		}
	}

done:
	*data_len = pos;
	return (int)pos;
}
