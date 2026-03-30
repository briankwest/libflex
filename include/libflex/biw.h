#ifndef LIBFLEX_BIW_H
#define LIBFLEX_BIW_H

#include "error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Block Information Word (BIW1) -- first codeword of frame data.
 * After BCH correction, the 21 data bits are:
 *   bits 0-3:   checksum (4-bit nibble sum = 0xF)
 *   bits 4-7:   BIW type / priority address count
 *   bits 8-9:   address field start offset (add 1 for actual word index)
 *   bits 10-15: vector field start word index
 *   bits 16-20: carry-on / flags
 */
typedef struct {
	uint8_t  priority_addr;     /* bits 4-7: priority address count */
	uint8_t  addr_field_start;  /* bits 8-9 + 1: first address word index */
	uint8_t  vect_field_start;  /* bits 10-15: first vector word index */
	uint8_t  carry_on;          /* bits 16-20: carry-on flags */
} flex_biw_t;

flex_err_t flex_biw_decode(uint32_t codeword, flex_biw_t *biw);
uint32_t   flex_biw_encode(const flex_biw_t *biw);

#ifdef __cplusplus
}
#endif

#endif
