#include "flex_internal.h"

/*
 * FLEX address encoding (from PDW Flex.cpp):
 *
 * Short address: single codeword
 *   data21 = capcode + 32768
 *   Valid short range: data21 in [0x008001, 0x1E0000] roughly
 *
 * Long address: two codewords
 *   word1 data21 = capcode (acts as short-addr trigger in reserved ranges)
 *   word2 data21 = second word, combined to form full capcode
 *   Full capcode = ((word2 ^ 0x1FFFFF) << 15) + 2068480 + word1
 *
 * Long address detection: data21 values in these ranges trigger long addr:
 *   data21 < 0x008001
 *   data21 in (0x1E0000, 0x1F0001)
 *   data21 > 0x1F7FFE
 */

/* Short address threshold */
#define SHORT_ADDR_OFFSET  32768u
#define LONG_ADDR_BASE     2068480u

uint32_t flex_cw_short_addr(uint32_t capcode)
{
	uint32_t data21 = (capcode + SHORT_ADDR_OFFSET) & 0x1FFFFFu;
	return flex_codeword_build(data21);
}

/*
 * Long address word 1: the first codeword uses a value in a reserved
 * range that signals "long address follows".  We use the range < 0x008001.
 * word1 data21 = (capcode & 0x1FFFFF) -- the low 21 bits serve as a key.
 */
uint32_t flex_cw_long_addr1(uint32_t capcode)
{
	/* low portion used as word1 identifier */
	uint32_t data21 = capcode & 0x1FFFFFu;
	/* ensure it falls in a long-address-trigger range */
	if (data21 >= 0x008001u)
		data21 = capcode & 0x7FFFu;  /* keep in < 0x8001 range */
	return flex_codeword_build(data21);
}

/*
 * Long address word 2: encodes the high portion.
 * capcode = ((word2 ^ 0x1FFFFF) << 15) + LONG_ADDR_BASE + word1
 * Solving for word2:
 *   word2 = ((capcode - LONG_ADDR_BASE - word1) >> 15) ^ 0x1FFFFF
 */
uint32_t flex_cw_long_addr2(uint32_t capcode)
{
	uint32_t word1_val = capcode & 0x7FFFu;
	uint32_t upper = (capcode - LONG_ADDR_BASE - word1_val) >> 15;
	uint32_t data21 = (upper ^ 0x1FFFFFu) & 0x1FFFFFu;
	return flex_codeword_build(data21);
}

/*
 * Vector codeword:
 *   bits 0-3:  checksum (nibble sum = 0xF)
 *   bits 4-6:  message type (0-7)
 *   bits 7-13: message start word index (0-127)
 *   bits 14-20: message word count (0-127)
 */
uint32_t flex_cw_vector(flex_msg_type_t type, uint16_t start_word,
                        uint16_t msg_words, int fragment)
{
	(void)fragment;

	uint32_t data21 = 0;
	data21 |= ((uint32_t)(type & 0x7)) << 4;
	data21 |= ((uint32_t)(start_word & 0x7F)) << 7;
	data21 |= ((uint32_t)(msg_words & 0x7F)) << 14;

	/* compute checksum: nibble sum = 0xF */
	unsigned int sum = 0;
	sum += (data21 >> 4) & 0xF;
	sum += (data21 >> 8) & 0xF;
	sum += (data21 >> 12) & 0xF;
	sum += (data21 >> 16) & 0xF;
	sum += (data21 >> 20) & 0x1;
	uint32_t checksum = (0xF - sum) & 0xF;
	data21 = (data21 & ~(uint32_t)0xF) | checksum;

	return flex_codeword_build(data21);
}

/*
 * Data codeword: 21 data bits directly.
 */
uint32_t flex_cw_data(uint32_t data21)
{
	return flex_codeword_build(data21 & 0x1FFFFFu);
}
