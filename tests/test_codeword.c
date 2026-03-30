#include "test.h"
#include <libflex/bch.h>
#include <libflex/types.h>

extern uint32_t flex_cw_short_addr(uint32_t capcode);
extern uint32_t flex_cw_long_addr1(uint32_t capcode);
extern uint32_t flex_cw_long_addr2(uint32_t capcode);
extern uint32_t flex_cw_vector(flex_msg_type_t type, uint16_t start_word,
                               uint16_t msg_words, int fragment);
extern uint32_t flex_cw_data(uint32_t data21);

static void test_cw_short_addr_bch(void)
{
	uint32_t cw = flex_cw_short_addr(1234567);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_cw_short_addr_zero(void)
{
	uint32_t cw = flex_cw_short_addr(0);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_cw_short_addr_decode(void)
{
	uint32_t capcode = 100000;
	uint32_t cw = flex_cw_short_addr(capcode);
	uint32_t data21 = cw >> 11;
	/* capcode = data21 - 32768 */
	ASSERT_EQ_U32(data21 - 32768, capcode);
}

static void test_cw_long_addr_bch(void)
{
	uint32_t cw1 = flex_cw_long_addr1(5000000);
	uint32_t cw2 = flex_cw_long_addr2(5000000);
	ASSERT_EQ_U32(flex_bch_syndrome(cw1), 0);
	ASSERT(flex_parity_check(cw1));
	ASSERT_EQ_U32(flex_bch_syndrome(cw2), 0);
	ASSERT(flex_parity_check(cw2));
}

static void test_cw_long_addr_range(void)
{
	/* word1 should be in long-address-trigger range (< 0x008001) */
	uint32_t cw1 = flex_cw_long_addr1(5000000);
	uint32_t data21 = cw1 >> 11;
	ASSERT(data21 < 0x008001u);
}

static void test_cw_vector_bch(void)
{
	uint32_t cw = flex_cw_vector(FLEX_MSG_ALPHA, 10, 5, 0);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_cw_vector_fields(void)
{
	uint32_t cw = flex_cw_vector(FLEX_MSG_NUMERIC, 20, 3, 0);
	uint32_t data21 = cw >> 11;

	uint32_t type = (data21 >> 4) & 0x7;
	uint32_t start = (data21 >> 7) & 0x7F;
	uint32_t count = (data21 >> 14) & 0x7F;

	ASSERT_EQ_U32(type, FLEX_MSG_NUMERIC);
	ASSERT_EQ_U32(start, 20);
	ASSERT_EQ_U32(count, 3);
}

static void test_cw_vector_checksum(void)
{
	uint32_t cw = flex_cw_vector(FLEX_MSG_BINARY, 50, 10, 0);
	uint32_t data21 = cw >> 11;

	/* verify nibble checksum = 0xF */
	unsigned int sum = 0;
	sum += (data21)       & 0xF;
	sum += (data21 >> 4)  & 0xF;
	sum += (data21 >> 8)  & 0xF;
	sum += (data21 >> 12) & 0xF;
	sum += (data21 >> 16) & 0xF;
	sum += (data21 >> 20) & 0x1;
	ASSERT_EQ_U32(sum & 0xF, 0xF);
}

static void test_cw_data_bch(void)
{
	uint32_t cw = flex_cw_data(0x1ABCDE & 0x1FFFFF);
	ASSERT_EQ_U32(flex_bch_syndrome(cw), 0);
	ASSERT(flex_parity_check(cw));
}

static void test_cw_data_roundtrip(void)
{
	uint32_t data = 0x0F0F0F;
	uint32_t cw = flex_cw_data(data);
	uint32_t extracted = cw >> 11;
	ASSERT_EQ_U32(extracted, data);
}

void test_codeword(void)
{
	printf("Codeword:\n");
	RUN_TEST(test_cw_short_addr_bch);
	RUN_TEST(test_cw_short_addr_zero);
	RUN_TEST(test_cw_short_addr_decode);
	RUN_TEST(test_cw_long_addr_bch);
	RUN_TEST(test_cw_long_addr_range);
	RUN_TEST(test_cw_vector_bch);
	RUN_TEST(test_cw_vector_fields);
	RUN_TEST(test_cw_vector_checksum);
	RUN_TEST(test_cw_data_bch);
	RUN_TEST(test_cw_data_roundtrip);
	printf("\n");
}
