#include "libflex/modem.h"
#include "flex_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================
 * Modulator
 * ================================================================ */

/*
 * Compute adaptive baseband frequencies from sample rate.
 * All 4 tones must fit between ~200 Hz and Nyquist (sr/2).
 * Center = sr * 0.20, outer deviation = sr * 0.13, inner = outer/3.
 * This ensures at 8 kHz:   tones = 240, 1167, 2433, 3360  (all < 4000)
 *              at 48 kHz:  tones = 3360, 7200, 12000, 15840 (all < 24000)
 */
static void compute_baseband_freqs(float sr, float *center, float *dev, float *inner)
{
	*center = sr * 0.20f;
	*dev    = sr * 0.13f;
	*inner  = *dev / 3.0f;
}

void flex_mod_init(flex_mod_t *mod, flex_speed_t speed, float sample_rate)
{
	memset(mod, 0, sizeof(*mod));
	mod->speed = speed;
	mod->sample_rate = sample_rate > 0 ? sample_rate : FLEX_MODEM_SAMPLE_RATE;
	compute_baseband_freqs(mod->sample_rate, &mod->center_freq,
	                       &mod->deviation, &mod->inner_dev);
	mod->phase = 0.0f;

	switch (speed) {
	case FLEX_SPEED_1600_2: mod->baud = 1600; break;
	case FLEX_SPEED_3200_2: mod->baud = 3200; break;
	case FLEX_SPEED_3200_4: mod->baud = 1600; break;
	case FLEX_SPEED_6400_4: mod->baud = 3200; break;
	}
}

/*
 * Map a bit (2-FSK) or bit-pair (4-FSK) to a frequency offset from center.
 *
 * 2-FSK:  0 → -deviation,  1 → +deviation
 * 4-FSK:  symbol 0 → -4800, 1 → -1600, 2 → +1600, 3 → +4800 (Gray coded)
 */
static float symbol_to_freq(const flex_mod_t *mod, int symbol, int is_4fsk)
{
	if (!is_4fsk) {
		return mod->center_freq + (symbol ? mod->deviation : -mod->deviation);
	}

	switch (symbol & 3) {
	case 0: return mod->center_freq - mod->deviation;
	case 1: return mod->center_freq - mod->inner_dev;
	case 2: return mod->center_freq + mod->inner_dev;
	case 3: return mod->center_freq + mod->deviation;
	}
	return mod->center_freq;
}

/*
 * Modulate bits to audio using a symbol clock phase accumulator.
 * This ensures the actual baud rate matches exactly, avoiding
 * timing drift from integer-sample rounding.
 */
flex_err_t flex_mod_bits(flex_mod_t *mod,
                         const uint8_t *bits, size_t nbits,
                         float *samples, size_t samples_cap,
                         size_t *nsamples)
{
	if (!mod || !bits || !samples || !nsamples)
		return FLEX_ERR_PARAM;

	int is_4fsk = flex_speed_is_4fsk(mod->speed);
	float samples_per_symbol = mod->sample_rate / (float)mod->baud;
	float sym_clock = 0.0f;  /* symbol clock accumulator */
	size_t written = 0;
	size_t bit_idx = 0;

	/* current symbol frequency */
	int symbol = 0;
	float freq = symbol_to_freq(mod, 0, is_4fsk);
	int need_next = 1;

	while (bit_idx < nbits || !need_next) {
		/* load next symbol if needed */
		if (need_next) {
			if (bit_idx >= nbits)
				break;
			if (is_4fsk && bit_idx + 1 < nbits) {
				symbol = ((bits[bit_idx] & 1) << 1) | (bits[bit_idx + 1] & 1);
				bit_idx += 2;
			} else {
				symbol = bits[bit_idx] & 1;
				bit_idx++;
			}
			freq = symbol_to_freq(mod, symbol, is_4fsk);
			need_next = 0;
		}

		/* generate samples for current symbol */
		float phase_inc = 2.0f * (float)M_PI * freq / mod->sample_rate;

		while (sym_clock < samples_per_symbol) {
			if (written >= samples_cap)
				goto done;
			samples[written++] = sinf(mod->phase);
			mod->phase += phase_inc;
			if (mod->phase > 2.0f * (float)M_PI)
				mod->phase -= 2.0f * (float)M_PI;
			sym_clock += 1.0f;
		}

		/* symbol complete: advance to next */
		sym_clock -= samples_per_symbol;
		need_next = 1;
	}

done:
	*nsamples = written;
	return FLEX_OK;
}

/* ================================================================
 * Demodulator
 * ================================================================ */

#define DC_ALPHA       0.001f
#define ENV_ALPHA      0.02f
#define PLL_LOCKED_R   0.045f
#define PLL_UNLOCKED_R 0.050f
#define LOCK_THRESHOLD 24

static int speed_to_baud(flex_speed_t speed)
{
	switch (speed) {
	case FLEX_SPEED_1600_2: return 1600;
	case FLEX_SPEED_3200_2: return 3200;
	case FLEX_SPEED_3200_4: return 1600;
	case FLEX_SPEED_6400_4: return 3200;
	}
	return 1600;
}

void flex_demod_init(flex_demod_t *demod, float sample_rate)
{
	memset(demod, 0, sizeof(*demod));
	demod->sample_rate = sample_rate > 0 ? sample_rate : FLEX_MODEM_SAMPLE_RATE;
	float dev, inner;
	compute_baseband_freqs(demod->sample_rate, &demod->center_freq, &dev, &inner);
	demod->baud = 1600;
	demod->phase_max = demod->sample_rate / (float)demod->baud;
	demod->phase = 0.0f;
}

void flex_demod_init_speed(flex_demod_t *demod, float sample_rate,
                           flex_speed_t speed)
{
	memset(demod, 0, sizeof(*demod));
	demod->sample_rate = sample_rate > 0 ? sample_rate : FLEX_MODEM_SAMPLE_RATE;
	float dev, inner;
	compute_baseband_freqs(demod->sample_rate, &demod->center_freq, &dev, &inner);
	demod->speed = speed;
	demod->baud = speed_to_baud(speed);
	demod->phase_max = demod->sample_rate / (float)demod->baud;
	demod->phase = 0.0f;
}

void flex_demod_reset(flex_demod_t *demod)
{
	float sr = demod->sample_rate;
	flex_demod_init(demod, sr);
}

/*
 * Goertzel-based FSK demodulator.
 *
 * 2-FSK: Goertzel at mark (+dev) and space (-dev) frequencies.
 *        Outputs 1 bit per symbol.
 *
 * 4-FSK: Goertzel at all 4 tone frequencies:
 *        sym 0: -4800 Hz  (space outer)
 *        sym 1: -1600 Hz  (space inner)
 *        sym 2: +1600 Hz  (mark inner)
 *        sym 3: +4800 Hz  (mark outer)
 *        Outputs 2 bits per symbol (matching encoder bit-pair packing).
 */

flex_err_t flex_demod_feed(flex_demod_t *demod,
                           const float *samples, size_t nsamples)
{
	if (!demod || !samples)
		return FLEX_ERR_PARAM;

	int is_4fsk = flex_speed_is_4fsk(demod->speed);

	/* compute adaptive baseband frequencies matching the modulator */
	float center, dev, inner;
	compute_baseband_freqs(demod->sample_rate, &center, &dev, &inner);

	float freq[4];
	int ntones;

	if (is_4fsk) {
		freq[0] = center - dev;
		freq[1] = center - inner;
		freq[2] = center + inner;
		freq[3] = center + dev;
		for (int i = 0; i < 4; i++)
			if (freq[i] < 0) freq[i] = -freq[i];
		ntones = 4;
	} else {
		freq[0] = center - dev;
		freq[1] = center + dev;
		if (freq[0] < 0) freq[0] = -freq[0];
		ntones = 2;
	}

	/* Goertzel coefficients */
	float w[4];
	for (int i = 0; i < ntones; i++)
		w[i] = 2.0f * cosf(2.0f * (float)M_PI * freq[i] / demod->sample_rate);

	/* Goertzel state per tone: s1, s2 */
	float gs1[4] = {0}, gs2[4] = {0};

	for (size_t n = 0; n < nsamples; n++) {
		float s = samples[n];

		/* Goertzel iteration for each tone */
		for (int t = 0; t < ntones; t++) {
			float s0 = s + w[t] * gs1[t] - gs2[t];
			gs2[t] = gs1[t];
			gs1[t] = s0;
		}

		demod->phase += 1.0f;

		/* symbol boundary */
		if (demod->phase >= demod->phase_max) {
			demod->phase -= demod->phase_max;

			/* compute power at each tone */
			float pwr[4];
			for (int t = 0; t < ntones; t++)
				pwr[t] = gs1[t]*gs1[t] + gs2[t]*gs2[t] - w[t]*gs1[t]*gs2[t];

			if (is_4fsk) {
				/* find strongest tone → symbol 0-3 */
				int best = 0;
				for (int t = 1; t < 4; t++)
					if (pwr[t] > pwr[best]) best = t;

				/* output 2 bits matching encoder packing:
				 * symbol = ((bits[i] & 1) << 1) | (bits[i+1] & 1)
				 * so bit0 = (symbol >> 1) & 1, bit1 = symbol & 1 */
				if (demod->out_count + 1 < sizeof(demod->out_bits)) {
					demod->out_bits[demod->out_count++] = (best >> 1) & 1;
					demod->out_bits[demod->out_count++] = best & 1;
				}
			} else {
				/* 2-FSK: space=tone[0], mark=tone[1] */
				int bit = (pwr[1] > pwr[0]) ? 1 : 0;
				if (demod->out_count < sizeof(demod->out_bits))
					demod->out_bits[demod->out_count++] = (uint8_t)bit;
			}

			/* reset Goertzel state */
			for (int t = 0; t < ntones; t++) {
				gs1[t] = 0.0f;
				gs2[t] = 0.0f;
			}
		}
	}

	return FLEX_OK;
}

/*
 * Baseband (FM discriminator output) demodulator.
 *
 * Input is post-discriminator audio: positive = higher frequency (mark),
 * negative = lower frequency (space).  This is what comes out of an FM
 * receiver's discriminator or an SDR FM demod chain.
 *
 * 2-FSK: accumulate sample level over symbol period, slice at zero.
 * The decoder handles sync detection and speed auto-detection from
 * the recovered bit stream.
 */
flex_err_t flex_demod_baseband(flex_demod_t *demod,
                               const float *samples, size_t nsamples)
{
	if (!demod || !samples)
		return FLEX_ERR_PARAM;

	for (size_t i = 0; i < nsamples; i++) {
		float s = samples[i];

		/* accumulate sample level over symbol period */
		demod->sym_accum += s;
		demod->sym_count++;

		/* advance symbol clock */
		demod->phase += 1.0f;

		/* symbol boundary */
		if (demod->phase >= demod->phase_max) {
			demod->phase -= demod->phase_max;

			if (demod->sym_count > 0) {
				float avg = demod->sym_accum / (float)demod->sym_count;

				/* 2-FSK: positive = bit 1, negative = bit 0 */
				int bit = (avg > 0) ? 1 : 0;

				if (demod->out_count < sizeof(demod->out_bits))
					demod->out_bits[demod->out_count++] = (uint8_t)bit;
			}

			demod->sym_accum = 0.0f;
			demod->sym_count = 0;
		}
	}

	return FLEX_OK;
}

/* ================================================================
 * WAV file I/O
 * ================================================================ */

/* Simple little-endian write helpers */
static void write_u16(FILE *f, uint16_t v)
{
	uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
	fwrite(buf, 1, 2, f);
}

static void write_u32(FILE *f, uint32_t v)
{
	uint8_t buf[4] = {
		(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
		(uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)
	};
	fwrite(buf, 1, 4, f);
}

static uint16_t read_u16(FILE *f)
{
	uint8_t buf[2];
	if (fread(buf, 1, 2, f) != 2) return 0;
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32(FILE *f)
{
	uint8_t buf[4];
	if (fread(buf, 1, 4, f) != 4) return 0;
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
	       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

flex_err_t flex_wav_write(const char *path, float sample_rate,
                          const float *samples, size_t nsamples)
{
	if (!path || !samples)
		return FLEX_ERR_PARAM;

	FILE *f = fopen(path, "wb");
	if (!f)
		return FLEX_ERR_PARAM;

	uint32_t sr = (uint32_t)sample_rate;
	uint16_t channels = 1;
	uint16_t bits_per_sample = 16;
	uint32_t byte_rate = sr * channels * bits_per_sample / 8;
	uint16_t block_align = channels * bits_per_sample / 8;
	uint32_t data_size = (uint32_t)(nsamples * block_align);
	uint32_t file_size = 36 + data_size;

	int err = 0;

	/* RIFF header */
	err |= (fwrite("RIFF", 1, 4, f) != 4);
	write_u32(f, file_size);
	err |= (fwrite("WAVE", 1, 4, f) != 4);

	/* fmt chunk */
	err |= (fwrite("fmt ", 1, 4, f) != 4);
	write_u32(f, 16);
	write_u16(f, 1);
	write_u16(f, channels);
	write_u32(f, sr);
	write_u32(f, byte_rate);
	write_u16(f, block_align);
	write_u16(f, bits_per_sample);

	/* data chunk */
	err |= (fwrite("data", 1, 4, f) != 4);
	write_u32(f, data_size);

	for (size_t i = 0; i < nsamples && !err; i++) {
		float s = samples[i];
		if (s > 1.0f) s = 1.0f;
		if (s < -1.0f) s = -1.0f;
		int16_t pcm = (int16_t)(s * 32767.0f);
		uint8_t buf[2] = { (uint8_t)(pcm & 0xFF), (uint8_t)((pcm >> 8) & 0xFF) };
		err |= (fwrite(buf, 1, 2, f) != 2);
	}

	fclose(f);
	return err ? FLEX_ERR_OVERFLOW : FLEX_OK;
}

flex_err_t flex_wav_read(const char *path, float *samples,
                         size_t samples_cap, size_t *nsamples,
                         float *file_sample_rate)
{
	if (!path || !samples || !nsamples || !file_sample_rate)
		return FLEX_ERR_PARAM;

	FILE *f = fopen(path, "rb");
	if (!f)
		return FLEX_ERR_PARAM;

	/* read RIFF header */
	char riff[4], wave[4];
	if (fread(riff, 1, 4, f) != 4 || (read_u32(f), 0) ||
	    fread(wave, 1, 4, f) != 4) {
		fclose(f);
		return FLEX_ERR_PARAM;
	}

	if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
		fclose(f);
		return FLEX_ERR_PARAM;
	}

	uint16_t channels = 0;
	uint16_t bits_per_sample = 0;
	uint32_t sr = 0;
	uint32_t data_size = 0;
	int found_fmt = 0, found_data = 0;

	/* parse chunks */
	while (!found_data) {
		char chunk_id[4];
		if (fread(chunk_id, 1, 4, f) != 4)
			break;
		uint32_t chunk_size = read_u32(f);

		if (chunk_size > 100000000u) {
			/* reject unreasonably large chunks (>100 MB) */
			fclose(f);
			return FLEX_ERR_PARAM;
		}

		if (memcmp(chunk_id, "fmt ", 4) == 0) {
			if (chunk_size < 16) { fclose(f); return FLEX_ERR_PARAM; }
			read_u16(f);  /* format (skip) */
			channels = read_u16(f);
			sr = read_u32(f);
			read_u32(f);  /* byte rate */
			read_u16(f);  /* block align */
			bits_per_sample = read_u16(f);
			if (chunk_size > 16)
				fseek(f, (long)(chunk_size - 16), SEEK_CUR);
			found_fmt = 1;
		} else if (memcmp(chunk_id, "data", 4) == 0) {
			data_size = chunk_size;
			found_data = 1;
		} else {
			fseek(f, (long)chunk_size, SEEK_CUR);
		}
	}

	if (!found_fmt || !found_data || channels == 0 ||
	    (bits_per_sample != 8 && bits_per_sample != 16)) {
		fclose(f);
		return FLEX_ERR_PARAM;
	}

	*file_sample_rate = (float)sr;

	size_t bytes_per_sample = bits_per_sample / 8;
	size_t total_samples = data_size / bytes_per_sample / channels;
	if (total_samples > samples_cap)
		total_samples = samples_cap;

	size_t read_count = 0;
	for (size_t i = 0; i < total_samples; i++) {
		int16_t pcm = 0;

		if (bits_per_sample == 16) {
			uint8_t buf[2];
			if (fread(buf, 1, 2, f) != 2) break;
			pcm = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
		} else if (bits_per_sample == 8) {
			uint8_t buf[1];
			if (fread(buf, 1, 1, f) != 1) break;
			pcm = (int16_t)((int)buf[0] - 128) * 256;
		} else {
			break;
		}

		/* skip extra channels */
		if (channels > 1)
			fseek(f, (long)((channels - 1) * bits_per_sample / 8), SEEK_CUR);

		samples[read_count++] = (float)pcm / 32768.0f;
	}

	*nsamples = read_count;
	fclose(f);
	return FLEX_OK;
}

/* ================================================================
 * Baseband NRZ/4-level output
 * ================================================================ */

/*
 * The encoder bitstream has a fixed header (preamble + sync1 + dotting
 * + FIW = 1072 bits) always at 1600 baud, followed by sync2 dotting
 * and data at the mode's baud rate.
 *
 * For 2-FSK: each bit maps to one sample level (±1.0).
 * For 4-FSK: pairs of phase bits are combined into 4-level symbols.
 *
 * However, the encoder already interleaves phases into the bitstream,
 * so for 2-FSK modes we emit one sample per bit, and for 4-FSK modes
 * the bits are already phase-interleaved — pairs of consecutive bits
 * represent (phase_A, phase_B) and map to a 4-level symbol.
 */

#define FLEX_HEADER_BITS  1072  /* preamble(960)+sync1(64)+dotting(16)+FIW(32) */

/* Gray code: (A=0,B=0)→-1.0, (A=0,B=1)→-0.33, (A=1,B=1)→+0.33, (A=1,B=0)→+1.0 */
static float gray_level(int bit_a, int bit_b)
{
	if (!bit_a && !bit_b) return -0.73f;
	if (!bit_a &&  bit_b) return -0.73f / 3.0f;
	if ( bit_a &&  bit_b) return  0.73f / 3.0f;
	return 0.73f;
}

flex_err_t flex_baseband(const uint8_t *bits, size_t nbits,
                         flex_speed_t speed, float sample_rate,
                         float *out, size_t out_cap,
                         size_t *out_len)
{
	return flex_baseband_ex(bits, nbits, speed, sample_rate,
	                        0, out, out_cap, out_len);
}

flex_err_t flex_baseband_ex(const uint8_t *bits, size_t nbits,
                            flex_speed_t speed, float sample_rate,
                            int flags,
                            float *out, size_t out_cap,
                            size_t *out_len)
{
	if (!bits || !out || !out_len)
		return FLEX_ERR_PARAM;
	if (sample_rate < 8000.0f || nbits == 0)
		return FLEX_ERR_PARAM;

	int data_baud;
	int is_4fsk = flex_speed_is_4fsk(speed);

	switch (speed) {
	case FLEX_SPEED_1600_2:  data_baud = 1600; break;
	case FLEX_SPEED_3200_2:  data_baud = 3200; break;
	case FLEX_SPEED_3200_4:  data_baud = 1600; break;
	case FLEX_SPEED_6400_4:  data_baud = 3200; break;
	default:                 data_baud = 1600; break;
	}

	/* Use 16-bit fixed-point phase accumulation matching multimon-ng's
	 * generator for exact timing compatibility at all sample rates. */
	unsigned int hdr_phinc  = (unsigned int)(0x10000u * 1600 / sample_rate);
	unsigned int data_phinc = (unsigned int)(0x10000u * data_baud / sample_rate);

	size_t bi = 0;
	size_t wi = 0;
	unsigned int bitph = 0;

	/* Match multimon-ng gen_flex.c timing: increment phase first,
	 * check for wrap (advance bit), THEN output current bit.
	 * Every loop iteration produces exactly one sample. */
	while (bi < nbits && wi < out_cap) {
		unsigned int phinc = (bi < FLEX_HEADER_BITS) ? hdr_phinc : data_phinc;

		/* advance phase — if wrap, move to next bit/symbol */
		bitph += phinc;
		if (bitph >= 0x10000u) {
			bitph &= 0xFFFFu;
			bi++;
			if (bi >= nbits) break;

			/* for 4-FSK data, consume the second bit of the pair */
			if (is_4fsk && bi >= FLEX_HEADER_BITS && (bi & 1) == 0)
				bi++;  /* skip — already consumed below */
		}

		int bit = (bits[bi / 8] >> (7 - (int)(bi & 7))) & 1;

		if (!is_4fsk || bi < FLEX_HEADER_BITS) {
			out[wi++] = bit ? 0.73f : -0.73f;
		} else {
			int bit_a = bit;
			int bit_b = 0;
			size_t bi2 = bi + 1;
			if (bi2 < nbits)
				bit_b = (bits[bi2 / 8] >> (7 - (int)(bi2 & 7))) & 1;
			out[wi++] = gray_level(bit_a, bit_b);
		}
	}

	/* Apply 75µs de-emphasis (first-order IIR lowpass) to pre-cancel
	 * the radio's TX pre-emphasis. */
	if ((flags & FLEX_BASEBAND_DEEMPH) && wi > 0) {
		float alpha = 1.0f / (1.0f + sample_rate * 75e-6f);
		float prev = out[0];
		for (size_t i = 1; i < wi; i++) {
			prev = prev + alpha * (out[i] - prev);
			out[i] = prev;
		}
	}

	*out_len = wi;
	return FLEX_OK;
}
