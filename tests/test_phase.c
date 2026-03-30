#include "test.h"
#include <libflex/types.h>
#include <string.h>

extern int flex_speed_phases(flex_speed_t speed);
extern int flex_speed_bps(flex_speed_t speed);
extern int flex_speed_is_4fsk(flex_speed_t speed);
extern uint16_t flex_capcode_to_frame(uint32_t capcode);

extern void flex_phase_separate(const uint8_t *symbols, size_t nsymbols,
                                flex_speed_t speed,
                                uint8_t phase_bits[][1024],
                                size_t *phase_bit_counts, int max_phases);
extern void flex_phase_combine(const uint8_t phase_bits[][1024],
                               size_t bits_per_phase, flex_speed_t speed,
                               uint8_t *symbols_out, size_t *nsymbols_out);

static void test_speed_phases(void)
{
	ASSERT_EQ_INT(flex_speed_phases(FLEX_SPEED_1600_2), 1);
	ASSERT_EQ_INT(flex_speed_phases(FLEX_SPEED_3200_2), 2);
	ASSERT_EQ_INT(flex_speed_phases(FLEX_SPEED_3200_4), 2);
	ASSERT_EQ_INT(flex_speed_phases(FLEX_SPEED_6400_4), 4);
}

static void test_speed_bps(void)
{
	ASSERT_EQ_INT(flex_speed_bps(FLEX_SPEED_1600_2), 1600);
	ASSERT_EQ_INT(flex_speed_bps(FLEX_SPEED_3200_2), 3200);
	ASSERT_EQ_INT(flex_speed_bps(FLEX_SPEED_3200_4), 3200);
	ASSERT_EQ_INT(flex_speed_bps(FLEX_SPEED_6400_4), 6400);
}

static void test_speed_4fsk(void)
{
	ASSERT_EQ_INT(flex_speed_is_4fsk(FLEX_SPEED_1600_2), 0);
	ASSERT_EQ_INT(flex_speed_is_4fsk(FLEX_SPEED_3200_2), 0);
	ASSERT_EQ_INT(flex_speed_is_4fsk(FLEX_SPEED_3200_4), 1);
	ASSERT_EQ_INT(flex_speed_is_4fsk(FLEX_SPEED_6400_4), 1);
}

static void test_capcode_to_frame(void)
{
	ASSERT_EQ_INT(flex_capcode_to_frame(0), 0);
	ASSERT_EQ_INT(flex_capcode_to_frame(127), 127);
	ASSERT_EQ_INT(flex_capcode_to_frame(128), 0);
	ASSERT_EQ_INT(flex_capcode_to_frame(129), 1);
	ASSERT_EQ_INT(flex_capcode_to_frame(1000000), 1000000 % 128);
}

static void test_phase_separate_1600(void)
{
	/* 1600/2: 1 phase, symbols are just bits */
	uint8_t symbols[] = { 1, 0, 1, 1, 0, 0, 1, 0 };
	uint8_t phase_bits[4][1024];
	size_t counts[4];

	flex_phase_separate(symbols, 8, FLEX_SPEED_1600_2,
	                    phase_bits, counts, 4);

	ASSERT_EQ_INT((int)counts[0], 8);
	ASSERT_EQ_INT(phase_bits[0][0], 1);
	ASSERT_EQ_INT(phase_bits[0][1], 0);
	ASSERT_EQ_INT(phase_bits[0][2], 1);
	ASSERT_EQ_INT(phase_bits[0][3], 1);
}

static void test_phase_roundtrip_1600(void)
{
	uint8_t symbols[16];
	for (int i = 0; i < 16; i++)
		symbols[i] = i & 1;

	uint8_t phase_bits[4][1024];
	size_t counts[4];
	flex_phase_separate(symbols, 16, FLEX_SPEED_1600_2,
	                    phase_bits, counts, 4);

	uint8_t out_symbols[32];
	size_t nsym = 0;
	flex_phase_combine((const uint8_t (*)[1024])phase_bits, counts[0],
	                   FLEX_SPEED_1600_2, out_symbols, &nsym);

	ASSERT_EQ_INT((int)nsym, 16);
	for (int i = 0; i < 16; i++)
		ASSERT_EQ_INT(out_symbols[i], symbols[i]);
}

static void test_phase_separate_4fsk(void)
{
	/* 6400/4: 4 phases from 4-FSK symbols */
	uint8_t symbols[] = { 0, 3, 1, 2 };
	uint8_t phase_bits[4][1024];
	size_t counts[4];

	flex_phase_separate(symbols, 4, FLEX_SPEED_6400_4,
	                    phase_bits, counts, 4);

	/* each symbol pair produces one bit per phase */
	ASSERT(counts[0] > 0);
	ASSERT(counts[1] > 0);
}

void test_phase(void)
{
	printf("Phase:\n");
	RUN_TEST(test_speed_phases);
	RUN_TEST(test_speed_bps);
	RUN_TEST(test_speed_4fsk);
	RUN_TEST(test_capcode_to_frame);
	RUN_TEST(test_phase_separate_1600);
	RUN_TEST(test_phase_roundtrip_1600);
	RUN_TEST(test_phase_separate_4fsk);
	printf("\n");
}
