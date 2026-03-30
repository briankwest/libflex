#ifndef LIBFLEX_ERROR_H
#define LIBFLEX_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	FLEX_OK              =  0,
	FLEX_ERR_PARAM       = -1,
	FLEX_ERR_OVERFLOW    = -2,
	FLEX_ERR_BADCHAR     = -3,
	FLEX_ERR_BCH         = -4,
	FLEX_ERR_SYNC        = -5,
	FLEX_ERR_STATE       = -6,
	FLEX_ERR_FIW         = -7,
	FLEX_ERR_INTERLEAVE  = -8,
	FLEX_ERR_SPEED       = -9,
	FLEX_ERR_PHASE       = -10,
	FLEX_ERR_FRAGMENT    = -11
} flex_err_t;

const char *flex_strerror(flex_err_t err);

#ifdef __cplusplus
}
#endif

#endif
