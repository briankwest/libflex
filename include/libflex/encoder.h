#ifndef LIBFLEX_ENCODER_H
#define LIBFLEX_ENCODER_H

#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	flex_msg_t    messages[FLEX_TX_MAX_MESSAGES];
	size_t        count;
	flex_speed_t  speed;
	uint16_t      cycle;
	uint16_t      frame;
} flex_encoder_t;

void       flex_encoder_init(flex_encoder_t *enc, flex_speed_t speed);
void       flex_encoder_reset(flex_encoder_t *enc);
void       flex_encoder_set_frame(flex_encoder_t *enc,
                                  uint16_t cycle, uint16_t frame);

flex_err_t flex_encoder_add(flex_encoder_t *enc,
                            uint32_t capcode,
                            flex_addr_type_t addr_type,
                            flex_msg_type_t type,
                            const char *text,
                            const uint8_t *binary_data,
                            size_t binary_len);

flex_err_t flex_encode(const flex_encoder_t *enc,
                       uint8_t *out, size_t out_cap,
                       size_t *out_len, size_t *out_bits);

flex_err_t flex_encode_single(uint32_t capcode,
                              flex_addr_type_t addr_type,
                              flex_msg_type_t type,
                              flex_speed_t speed,
                              const char *text,
                              const uint8_t *binary_data,
                              size_t binary_len,
                              uint8_t *out, size_t out_cap,
                              size_t *out_len, size_t *out_bits);

#ifdef __cplusplus
}
#endif

#endif
