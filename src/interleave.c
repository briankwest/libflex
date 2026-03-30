#include "flex_internal.h"

/*
 * FLEX block interleaving: 8 codewords (32 bits each) arranged as an
 * 8-row x 32-column matrix.  Transmitted column-by-column (LSB column first).
 *
 * Interleave (encoder): codewords in, bit stream out.
 *   Bit order: col 0 (bit 0) of cw[0..7], then col 1 (bit 1), ..., col 31 (bit 31).
 *
 * Deinterleave (decoder): bit stream in, codewords out.
 *   Received bit n:  row = n % ncw,  col = n / ncw
 *   Maps to: cw[row] bit col
 */

void flex_interleave(const uint32_t *cws_in, int ncw,
                     uint8_t *bits_out, int *nbits_out)
{
	if (!cws_in || !bits_out || !nbits_out || ncw <= 0) {
		if (nbits_out) *nbits_out = 0;
		return;
	}

	int total = ncw * 32;
	int idx = 0;

	for (int col = 0; col < 32; col++) {
		for (int row = 0; row < ncw; row++) {
			bits_out[idx++] = (cws_in[row] >> col) & 1;
		}
	}

	*nbits_out = total;
}

void flex_deinterleave(const uint8_t *bits_in, int nbits,
                       uint32_t *cws_out, int *ncw_out)
{
	if (!bits_in || !cws_out || !ncw_out || nbits <= 0) {
		if (ncw_out) *ncw_out = 0;
		return;
	}

	int ncw = nbits / 32;
	if (ncw <= 0) {
		*ncw_out = 0;
		return;
	}

	memset(cws_out, 0, (size_t)ncw * sizeof(uint32_t));

	for (int i = 0; i < nbits; i++) {
		int row = i % ncw;
		int col = i / ncw;
		if (col >= 32)
			break;
		if (bits_in[i])
			cws_out[row] |= (1u << col);
	}

	*ncw_out = ncw;
}
