#include "flex_internal.h"

int flex_speed_phases(flex_speed_t speed)
{
	switch (speed) {
	case FLEX_SPEED_1600_2: return 1;
	case FLEX_SPEED_3200_2: return 2;
	case FLEX_SPEED_3200_4: return 2;
	case FLEX_SPEED_6400_4: return 4;
	}
	return 1;
}

int flex_speed_bps(flex_speed_t speed)
{
	switch (speed) {
	case FLEX_SPEED_1600_2: return 1600;
	case FLEX_SPEED_3200_2: return 3200;
	case FLEX_SPEED_3200_4: return 3200;
	case FLEX_SPEED_6400_4: return 6400;
	}
	return 1600;
}

int flex_speed_is_4fsk(flex_speed_t speed)
{
	return (speed == FLEX_SPEED_3200_4 || speed == FLEX_SPEED_6400_4);
}

uint16_t flex_capcode_to_frame(uint32_t capcode)
{
	return (uint16_t)(capcode % FLEX_FRAMES_PER_CYCLE);
}

/*
 * 4-FSK symbol to phase bit mapping (from multimon-ng):
 *   symbol 0 → bit_a=0, bit_b=0
 *   symbol 1 → bit_a=0, bit_b=1
 *   symbol 2 → bit_a=1, bit_b=1
 *   symbol 3 → bit_a=1, bit_b=0
 *
 * At 3200/4 (2 phases): alternating symbols go to phase A/C
 *   even symbols → phase A (bit_a), phase B (bit_b)
 *   odd symbols  → phase C (bit_a), phase D (bit_b)
 *
 * At 6400/4 (4 phases): same pattern with all 4 phases active.
 *
 * At 2-FSK modes: each bit is one phase-A bit.
 */

void flex_phase_separate(const uint8_t *symbols, size_t nsymbols,
                         flex_speed_t speed,
                         uint8_t phase_bits[][1024],
                         size_t *phase_bit_counts, int max_phases)
{
	int nphases = flex_speed_phases(speed);

	for (int p = 0; p < max_phases; p++)
		phase_bit_counts[p] = 0;

	if (nphases == 1) {
		/* 2-FSK: symbols are just bits for phase A */
		size_t count = nsymbols < 1024 ? nsymbols : 1024;
		for (size_t i = 0; i < count; i++)
			phase_bits[0][i] = symbols[i] & 1;
		phase_bit_counts[0] = count;
		return;
	}

	if (nphases >= 2) {
		int toggle = 0;
		for (size_t i = 0; i < nsymbols; i++) {
			uint8_t sym = symbols[i] & 0x3;
			uint8_t bit_a = (sym > 1) ? 1 : 0;
			uint8_t bit_b = (sym == 1 || sym == 2) ? 1 : 0;

			if (toggle == 0) {
				/* phase A, B */
				if (phase_bit_counts[0] < 1024)
					phase_bits[0][phase_bit_counts[0]++] = bit_a;
				if (nphases >= 4 && max_phases >= 2 && phase_bit_counts[1] < 1024)
					phase_bits[1][phase_bit_counts[1]++] = bit_b;
				else if (nphases == 2 && max_phases >= 2 && phase_bit_counts[1] < 1024)
					phase_bits[1][phase_bit_counts[1]++] = bit_b;
			} else {
				/* phase C, D */
				int pc = (nphases >= 4) ? 2 : 0;
				int pd = (nphases >= 4) ? 3 : 1;
				if (pc < max_phases && phase_bit_counts[pc] < 1024)
					phase_bits[pc][phase_bit_counts[pc]++] = bit_a;
				if (pd < max_phases && phase_bit_counts[pd] < 1024)
					phase_bits[pd][phase_bit_counts[pd]++] = bit_b;
			}
			toggle ^= 1;
		}
	}
}

void flex_phase_combine(const uint8_t phase_bits[][1024],
                        size_t bits_per_phase, flex_speed_t speed,
                        uint8_t *symbols_out, size_t *nsymbols_out)
{
	int nphases = flex_speed_phases(speed);
	*nsymbols_out = 0;

	if (nphases == 1) {
		/* 2-FSK: phase A bits are the symbols */
		for (size_t i = 0; i < bits_per_phase; i++)
			symbols_out[(*nsymbols_out)++] = phase_bits[0][i] & 1;
		return;
	}

	/* 4-FSK: interleave phases back into symbols */
	for (size_t i = 0; i < bits_per_phase; i++) {
		uint8_t bit_a, bit_b;

		/* even index → phases A/B, odd → phases C/D */
		if (nphases >= 4) {
			bit_a = phase_bits[0][i];
			bit_b = phase_bits[1][i];
		} else {
			bit_a = phase_bits[0][i];
			bit_b = phase_bits[1][i];
		}

		/* map back to symbol: bit_a, bit_b → symbol */
		uint8_t sym;
		if (!bit_a && !bit_b)      sym = 0;
		else if (!bit_a && bit_b)  sym = 1;
		else if (bit_a && bit_b)   sym = 2;
		else                       sym = 3;
		symbols_out[(*nsymbols_out)++] = sym;

		if (nphases >= 4) {
			bit_a = phase_bits[2][i];
			bit_b = phase_bits[3][i];
		} else {
			bit_a = phase_bits[0][i];  /* will be unused in 2-phase */
			bit_b = phase_bits[1][i];
			/* for 2-phase, we already emitted the symbol above.
			 * The odd-toggle symbol draws from the same phases. */
			/* Actually for 3200/4 with 2 phases, alternating symbols
			 * come from A/B then C/D. With only 2 phases, odd symbols
			 * re-use phase 0/1. */
			continue;  /* skip second symbol for 2-phase */
		}

		if (!bit_a && !bit_b)      sym = 0;
		else if (!bit_a && bit_b)  sym = 1;
		else if (bit_a && bit_b)   sym = 2;
		else                       sym = 3;
		symbols_out[(*nsymbols_out)++] = sym;
	}
}
