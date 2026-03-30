#include "libflex/fiw.h"
#include "libflex/bch.h"

/*
 * Frame Information Word (FIW) layout (21 data bits after BCH decode):
 *   bits 0-3:   checksum (nibble sum of all nibbles + top bit = 0xF)
 *   bits 4-7:   cycle number (0-14)
 *   bits 8-14:  frame number (0-127)
 *   bits 15-20: reserved / fixed
 *
 * Checksum: sum of 4-bit nibbles across all 21 bits must equal 0xF.
 */

static int fiw_checksum_valid(uint32_t data21)
{
	unsigned int sum = 0;
	sum += (data21)       & 0xF;
	sum += (data21 >> 4)  & 0xF;
	sum += (data21 >> 8)  & 0xF;
	sum += (data21 >> 12) & 0xF;
	sum += (data21 >> 16) & 0xF;
	sum += (data21 >> 20) & 0x1;
	return (sum & 0xF) == 0xF;
}

static uint32_t fiw_compute_checksum(uint32_t data21)
{
	/* compute checksum nibble such that nibble sum = 0xF */
	unsigned int sum = 0;
	sum += (data21 >> 4)  & 0xF;
	sum += (data21 >> 8)  & 0xF;
	sum += (data21 >> 12) & 0xF;
	sum += (data21 >> 16) & 0xF;
	sum += (data21 >> 20) & 0x1;
	unsigned int needed = (0xF - sum) & 0xF;
	return (data21 & ~(uint32_t)0xF) | needed;
}

flex_err_t flex_fiw_decode(uint32_t codeword, flex_fiw_t *fiw)
{
	if (!fiw)
		return FLEX_ERR_PARAM;

	/* BCH error correction */
	if (flex_bch_correct(&codeword) < 0)
		return FLEX_ERR_BCH;

	/* extract 21 data bits */
	uint32_t data21 = codeword >> 11;

	/* validate checksum */
	if (!fiw_checksum_valid(data21))
		return FLEX_ERR_FIW;

	fiw->cycle  = (data21 >> 4) & 0xF;
	fiw->frame  = (data21 >> 8) & 0x7F;
	fiw->repeat = (data21 >> 15) & 0x3F;

	/* sanity check */
	if (fiw->cycle > 14 || fiw->frame > 127)
		return FLEX_ERR_FIW;

	return FLEX_OK;
}

uint32_t flex_fiw_encode(const flex_fiw_t *fiw)
{
	if (!fiw)
		return 0;

	uint32_t data21 = 0;
	data21 |= ((uint32_t)(fiw->cycle & 0xF)) << 4;
	data21 |= ((uint32_t)(fiw->frame & 0x7F)) << 8;
	data21 |= ((uint32_t)(fiw->repeat & 0x3F)) << 15;

	/* compute and insert checksum */
	data21 = fiw_compute_checksum(data21);

	return flex_codeword_build(data21);
}
