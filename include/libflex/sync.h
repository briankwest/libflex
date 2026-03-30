#ifndef LIBFLEX_SYNC_H
#define LIBFLEX_SYNC_H

#include "types.h"
#include "error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 32-bit sync marker: always transmitted at 1600 bps 2-FSK */
#define FLEX_SYNC_MARKER  0xA6C6AAAAu

/* 16-bit mode words that follow the sync marker (identify speed) */
#define FLEX_MODE_1600_2  0x870Cu    /* 1600 baud, 2-level FSK, 1 phase  */
#define FLEX_MODE_1600_4  0xB068u    /* 1600 baud, 4-level FSK, 2 phases */
#define FLEX_MODE_3200_2  0x7B18u    /* 3200 baud, 2-level FSK, 2 phases */
#define FLEX_MODE_3200_4  0xDEA0u    /* 3200 baud, 4-level FSK, 4 phases */

/* Identify speed from the 16-bit mode word following the sync marker */
flex_err_t flex_sync_detect_speed(uint16_t mode_word, flex_speed_t *speed);

/* Hamming distance between two 16-bit values */
int flex_hamming16(uint16_t a, uint16_t b);

/* Build Sync1 field: sync marker + mode word for a given speed.
 * Writes to out[] as packed 32-bit words. Sets *out_bits to total bit count. */
flex_err_t flex_sync1_build(flex_speed_t speed,
                            uint8_t *out, size_t out_cap,
                            size_t *out_bits);

/* Build Sync2 field: sync marker at the data rate.
 * Writes to out[]. Sets *out_bits to total bit count. */
flex_err_t flex_sync2_build(uint8_t *out, size_t out_cap,
                            size_t *out_bits);

#ifdef __cplusplus
}
#endif

#endif
