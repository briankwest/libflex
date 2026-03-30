#include "libflex/error.h"

const char *flex_strerror(flex_err_t err)
{
	switch (err) {
	case FLEX_OK:              return "success";
	case FLEX_ERR_PARAM:       return "invalid parameter";
	case FLEX_ERR_OVERFLOW:    return "buffer overflow";
	case FLEX_ERR_BADCHAR:     return "invalid character";
	case FLEX_ERR_BCH:         return "uncorrectable BCH error";
	case FLEX_ERR_SYNC:        return "sync lost";
	case FLEX_ERR_STATE:       return "invalid state";
	case FLEX_ERR_FIW:         return "invalid frame info word";
	case FLEX_ERR_INTERLEAVE:  return "deinterleave failure";
	case FLEX_ERR_SPEED:       return "unrecognized speed";
	case FLEX_ERR_PHASE:       return "phase error";
	case FLEX_ERR_FRAGMENT:    return "fragment reassembly failure";
	}
	return "unknown error";
}
