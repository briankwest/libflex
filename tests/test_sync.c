#include "test.h"
#include <libflex/sync.h>
#include <libflex/types.h>
#include <string.h>

static void test_sync_detect_1600_2(void)
{
	flex_speed_t speed;
	ASSERT_EQ_INT(flex_sync_detect_speed(FLEX_MODE_1600_2, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_1600_2);
}

static void test_sync_detect_3200_2(void)
{
	flex_speed_t speed;
	ASSERT_EQ_INT(flex_sync_detect_speed(FLEX_MODE_3200_2, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_3200_2);
}

static void test_sync_detect_3200_4(void)
{
	flex_speed_t speed;
	ASSERT_EQ_INT(flex_sync_detect_speed(FLEX_MODE_3200_4, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_3200_4);
}

static void test_sync_detect_6400_4(void)
{
	flex_speed_t speed;
	ASSERT_EQ_INT(flex_sync_detect_speed(FLEX_MODE_6400_4, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_6400_4);
}

static void test_sync_detect_fuzzy(void)
{
	/* flip 1 bit in the mode word -- should still match */
	flex_speed_t speed;
	uint16_t corrupted = FLEX_MODE_1600_2 ^ 0x0001;
	ASSERT_EQ_INT(flex_sync_detect_speed(corrupted, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_1600_2);
}

static void test_sync_detect_fuzzy_3bit(void)
{
	/* flip 3 bits -- should still match (within tolerance) */
	flex_speed_t speed;
	uint16_t corrupted = FLEX_MODE_6400_4 ^ 0x0007;
	ASSERT_EQ_INT(flex_sync_detect_speed(corrupted, &speed), FLEX_OK);
	ASSERT_EQ_INT(speed, FLEX_SPEED_6400_4);
}

static void test_sync_detect_invalid(void)
{
	/* totally wrong mode word */
	flex_speed_t speed;
	ASSERT_EQ_INT(flex_sync_detect_speed(0x0000, &speed), FLEX_ERR_SYNC);
}

static void test_sync_hamming(void)
{
	ASSERT_EQ_INT(flex_hamming16(0x0000, 0x0000), 0);
	ASSERT_EQ_INT(flex_hamming16(0x0001, 0x0000), 1);
	ASSERT_EQ_INT(flex_hamming16(0xFFFF, 0x0000), 16);
	ASSERT_EQ_INT(flex_hamming16(0xAAAA, 0x5555), 16);
}

static void test_sync1_build(void)
{
	uint8_t buf[16];
	size_t bits = 0;
	ASSERT_EQ_INT(flex_sync1_build(FLEX_SPEED_1600_2, buf, sizeof(buf), &bits), FLEX_OK);
	/* 16 bit-sync + 32 marker + 16 mode = 64 bits */
	ASSERT_EQ_INT((int)bits, 64);
}

static void test_sync1_build_all_speeds(void)
{
	flex_speed_t speeds[] = {
		FLEX_SPEED_1600_2, FLEX_SPEED_3200_2,
		FLEX_SPEED_3200_4, FLEX_SPEED_6400_4
	};
	for (int i = 0; i < 4; i++) {
		uint8_t buf[16];
		size_t bits = 0;
		ASSERT_EQ_INT(flex_sync1_build(speeds[i], buf, sizeof(buf), &bits),
		              FLEX_OK);
		ASSERT_EQ_INT((int)bits, 64);
	}
}

static void test_sync2_build(void)
{
	uint8_t buf[8];
	size_t bits = 0;
	ASSERT_EQ_INT(flex_sync2_build(buf, sizeof(buf), &bits), FLEX_OK);
	ASSERT_EQ_INT((int)bits, 32);
}

void test_sync(void)
{
	printf("Sync:\n");
	RUN_TEST(test_sync_detect_1600_2);
	RUN_TEST(test_sync_detect_3200_2);
	RUN_TEST(test_sync_detect_3200_4);
	RUN_TEST(test_sync_detect_6400_4);
	RUN_TEST(test_sync_detect_fuzzy);
	RUN_TEST(test_sync_detect_fuzzy_3bit);
	RUN_TEST(test_sync_detect_invalid);
	RUN_TEST(test_sync_hamming);
	RUN_TEST(test_sync1_build);
	RUN_TEST(test_sync1_build_all_speeds);
	RUN_TEST(test_sync2_build);
	printf("\n");
}
