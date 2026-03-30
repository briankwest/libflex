#ifndef LIBFLEX_FIW_H
#define LIBFLEX_FIW_H

#include "error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint16_t cycle;     /* 0-14 */
	uint16_t frame;     /* 0-127 */
	uint8_t  repeat;
} flex_fiw_t;

flex_err_t flex_fiw_decode(uint32_t codeword, flex_fiw_t *fiw);
uint32_t   flex_fiw_encode(const flex_fiw_t *fiw);

#ifdef __cplusplus
}
#endif

#endif
