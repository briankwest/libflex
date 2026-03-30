#include "libflex/bch.h"
#include "libflex/types.h"

#define BCH_GEN FLEX_BCH_POLY

static uint32_t reverse_bits_n(uint32_t val, int nbits)
{
	uint32_t r = 0;
	for (int i = 0; i < nbits; i++)
		if (val & (1u << i))
			r |= 1u << (nbits - 1 - i);
	return r;
}

uint32_t flex_bch_encode(uint32_t data21)
{
	/* Reverse data bits, standard poly division, reverse parity */
	uint32_t rev = reverse_bits_n(data21 & 0x1FFFFFu, 21);
	uint32_t reg = rev << 10;
	for (int i = 30; i >= 10; i--)
		if (reg & (1u << i))
			reg ^= (BCH_GEN << (i - 10));
	return reverse_bits_n(reg & 0x3FFu, 10);
}

uint32_t flex_codeword_build(uint32_t data21)
{
	data21 &= 0x1FFFFFu;
	return data21 | (flex_bch_encode(data21) << 21);
}

uint32_t flex_bch_syndrome(uint32_t codeword)
{
	/* Extract data and parity */
	uint32_t data21 = codeword & 0x1FFFFFu;
	uint32_t stored_parity = (codeword >> 21) & 0x3FFu;
	uint32_t computed_parity = flex_bch_encode(data21);
	return stored_parity ^ computed_parity;
}

int flex_parity_check(uint32_t codeword)
{
	return flex_bch_syndrome(codeword) == 0 ? 1 : 0;
}

int flex_bch_correct(uint32_t *codeword)
{
	uint32_t syn = flex_bch_syndrome(*codeword);
	if (syn == 0)
		return 0;

	/* Try single-bit corrections */
	for (int i = 0; i < 31; i++) {
		uint32_t trial = *codeword ^ (1u << i);
		if (flex_bch_syndrome(trial) == 0) {
			*codeword = trial;
			return 1;
		}
	}

	/* Try two-bit corrections */
	for (int i = 0; i < 31; i++) {
		for (int j = i + 1; j < 31; j++) {
			uint32_t trial = *codeword ^ (1u << i) ^ (1u << j);
			if (flex_bch_syndrome(trial) == 0) {
				*codeword = trial;
				return 2;
			}
		}
	}

	return -1;  /* uncorrectable */
}
