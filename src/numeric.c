#include "flex_internal.h"

static const char bcd_table[] = "0123456789*U -[]";

static int char_to_bcd(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	switch (c) {
	case '*':           return 0xA;
	case 'U': case 'u': return 0xB;
	case ' ':           return 0xC;
	case '-':           return 0xD;
	case '[': case '(': return 0xE;
	case ']': case ')': return 0xF;
	}
	return -1;
}

int flex_numeric_encode(const char *text, size_t len,
                        uint32_t *chunks, int max_chunks)
{
	if (!text || !chunks)
		return -1;

	int nchunks = 0;
	uint32_t chunk = 0;
	int digits = 0;

	for (size_t i = 0; i < len; i++) {
		int bcd = char_to_bcd(text[i]);
		if (bcd < 0)
			return -1;

		chunk |= ((uint32_t)bcd << (16 - digits * 4));
		digits++;

		if (digits == 5) {
			if (nchunks >= max_chunks)
				return -1;
			chunks[nchunks++] = chunk;
			chunk = 0;
			digits = 0;
		}
	}

	/* flush partial chunk (pad with space=0xC) */
	if (digits > 0) {
		while (digits < 5) {
			chunk |= (0xCu << (16 - digits * 4));
			digits++;
		}
		if (nchunks >= max_chunks)
			return -1;
		chunks[nchunks++] = chunk;
	}

	return nchunks;
}

int flex_numeric_decode(const uint32_t *chunks, int nchunks,
                        char *text, size_t text_cap)
{
	if (!chunks || !text || text_cap == 0)
		return -1;

	size_t pos = 0;

	for (int c = 0; c < nchunks; c++) {
		for (int d = 0; d < 5; d++) {
			int bcd = (chunks[c] >> (16 - d * 4)) & 0xF;
			if (pos + 1 >= text_cap)
				goto done;
			text[pos++] = bcd_table[bcd];
		}
	}

done:
	/* trim trailing spaces */
	while (pos > 0 && text[pos - 1] == ' ')
		pos--;

	text[pos] = '\0';
	return (int)pos;
}
