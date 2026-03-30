#ifndef LIBFLEX_BCH_H
#define LIBFLEX_BCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t flex_bch_encode(uint32_t data21);
uint32_t flex_codeword_build(uint32_t data21);
uint32_t flex_bch_syndrome(uint32_t codeword);
int      flex_bch_correct(uint32_t *codeword);   /* corrects up to 2 bits */
int      flex_parity_check(uint32_t codeword);

#ifdef __cplusplus
}
#endif

#endif
