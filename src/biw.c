#include "libflex/biw.h"
#include "libflex/bch.h"

/*
 * Block Information Word (BIW1) layout (21 data bits after BCH decode):
 *   bits 0-3:   checksum (nibble sum = 0xF, same as FIW)
 *   bits 4-7:   priority address count
 *   bits 8-9:   address field start offset (actual start = value + 1)
 *   bits 10-15: vector field start word index
 *   bits 16-20: carry-on / flags
 */

flex_err_t flex_biw_decode(uint32_t codeword, flex_biw_t *biw)
{
	if (!biw)
		return FLEX_ERR_PARAM;

	/* BCH error correction */
	if (flex_bch_correct(&codeword) < 0)
		return FLEX_ERR_BCH;

	uint32_t data21 = codeword & 0x1FFFFFu;

	biw->priority_addr    = (data21 >> 4) & 0xF;
	biw->addr_field_start = ((data21 >> 8) & 0x3) + 1;
	biw->vect_field_start = (data21 >> 10) & 0x3F;
	biw->carry_on         = (data21 >> 16) & 0x1F;

	return FLEX_OK;
}

uint32_t flex_biw_encode(const flex_biw_t *biw)
{
	if (!biw)
		return 0;

	uint32_t data21 = 0;
	data21 |= ((uint32_t)(biw->priority_addr & 0xF)) << 4;
	data21 |= ((uint32_t)((biw->addr_field_start - 1) & 0x3)) << 8;
	data21 |= ((uint32_t)(biw->vect_field_start & 0x3F)) << 10;
	data21 |= ((uint32_t)(biw->carry_on & 0x1F)) << 16;

	return flex_codeword_build(data21);
}
