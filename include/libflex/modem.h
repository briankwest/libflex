#ifndef LIBFLEX_MODEM_H
#define LIBFLEX_MODEM_H

#include "types.h"
#include "error.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Default parameters ---- */
#define FLEX_MODEM_SAMPLE_RATE   48000
#define FLEX_MODEM_DEVIATION     4800.0f   /* ±4800 Hz for outer symbols */
#define FLEX_MODEM_INNER_DEV     1600.0f   /* ±1600 Hz for inner 4-FSK symbols */
#define FLEX_MODEM_CENTER_FREQ   1800.0f   /* baseband center for audio output */

/*
 * 4-FSK symbol-to-frequency mapping (Gray coded):
 *   symbol 0 → -4800 Hz
 *   symbol 1 → -1600 Hz
 *   symbol 2 → +1600 Hz
 *   symbol 3 → +4800 Hz
 *
 * 2-FSK:
 *   bit 0 → -4800 Hz
 *   bit 1 → +4800 Hz
 */

/* ---- Modulator ---- */

typedef struct {
	flex_speed_t speed;
	float        sample_rate;
	float        center_freq;
	float        deviation;     /* outer deviation (±4800) */
	float        inner_dev;     /* inner deviation (±1600) for 4-FSK */
	float        phase;         /* current oscillator phase (radians) */
	int          baud;          /* symbol rate */
} flex_mod_t;

void flex_mod_init(flex_mod_t *mod, flex_speed_t speed, float sample_rate);

/* Modulate a bitstream to audio samples.
 * Input:  bits[] (one bit per byte, 0 or 1), nbits count
 * Output: samples[] (float, -1.0 to +1.0), *nsamples written
 * Returns FLEX_OK on success. */
flex_err_t flex_mod_bits(flex_mod_t *mod,
                         const uint8_t *bits, size_t nbits,
                         float *samples, size_t samples_cap,
                         size_t *nsamples);

/* ---- Demodulator ---- */

typedef struct {
	flex_speed_t speed;
	float        sample_rate;
	int          baud;

	/* PLL */
	float        phase;
	float        phase_rate;
	float        phase_max;
	int          locked;
	int          lock_count;

	/* Frequency discriminator (quadrature delay) */
	float        prev_i;        /* previous in-phase sample */
	float        prev_q;        /* previous quadrature sample */
	float        center_freq;   /* expected center frequency */

	/* DC offset removal on discriminator output */
	float        dc_offset;

	/* Envelope tracking */
	float        envelope;

	/* Symbol accumulation */
	float        sym_accum;
	int          sym_count;
	int          sym_last_level;

	/* Output buffer */
	uint8_t      out_bits[16384];
	size_t       out_count;
} flex_demod_t;

void flex_demod_init(flex_demod_t *demod, float sample_rate);

/* Initialize demodulator with explicit baud rate (for higher-speed modes).
 * Use this when decoding 3200/6400 bps frames where the modulator
 * uses a higher symbol rate. */
void flex_demod_init_speed(flex_demod_t *demod, float sample_rate,
                           flex_speed_t speed);

/* Feed audio samples to the demodulator.
 * After calling, read demod->out_bits[0..demod->out_count-1] for
 * recovered bits and reset demod->out_count = 0. */
flex_err_t flex_demod_feed(flex_demod_t *demod,
                           const float *samples, size_t nsamples);

/* Reset demodulator state */
void flex_demod_reset(flex_demod_t *demod);

/* ---- WAV file helpers ---- */

/* Write 16-bit PCM WAV header + float samples to file.
 * samples are -1.0 to +1.0, converted to int16. */
flex_err_t flex_wav_write(const char *path, float sample_rate,
                          const float *samples, size_t nsamples);

/* Read 16-bit PCM WAV file into float samples (-1.0 to +1.0).
 * Caller provides buffer. *nsamples set to count read.
 * *file_sample_rate set to the WAV file's sample rate. */
flex_err_t flex_wav_read(const char *path, float *samples,
                         size_t samples_cap, size_t *nsamples,
                         float *file_sample_rate);

#ifdef __cplusplus
}
#endif

#endif
