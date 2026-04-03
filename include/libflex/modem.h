#ifndef LIBFLEX_MODEM_H
#define LIBFLEX_MODEM_H

#include "types.h"
#include "error.h"
#include "decoder.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Default parameters ---- */
#define FLEX_MODEM_LEADIN_MS     500    /* silence before preamble */
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

/* Generate baseband NRZ/4-level samples from a bitstream.
 *
 * Produces flat-level-per-symbol output suitable for feeding an FM
 * transmitter or for direct decoding by multimon-ng.
 *
 * The encoder output (from flex_encode/flex_encode_single) includes
 * preamble, sync, FIW, dotting, and interleaved data.  This function
 * converts that bitstream to baseband samples at the given sample rate.
 *
 * 2-FSK modes: bit 1 → +1.0, bit 0 → -1.0
 * 4-FSK modes: symbols encoded as ±1/3 and ±1.0
 *
 * The baud rate changes mid-frame (1600 for header, mode baud for
 * data).  The function handles this internally based on frame structure.
 *
 * bits:        packed byte array (MSB-first, from flex_encode)
 * nbits:       total bits in the frame
 * speed:       FLEX speed mode (determines baud rate and FSK levels)
 * sample_rate: output sample rate in Hz
 * out:         output float sample buffer
 * out_cap:     capacity of out in samples
 * out_len:     receives the actual number of samples written
 */
flex_err_t flex_baseband(const uint8_t *bits, size_t nbits,
                         flex_speed_t speed, float sample_rate,
                         float *out, size_t out_cap,
                         size_t *out_len);

/* Baseband flags for flex_baseband_ex() */
#define FLEX_BASEBAND_DEEMPH  0x01  /* apply 75µs de-emphasis to cancel
                                     * radio TX pre-emphasis */

/* Extended baseband with flags.
 * Same as flex_baseband() but accepts flags for signal conditioning.
 * FLEX_BASEBAND_DEEMPH: apply a 75µs first-order IIR lowpass so that
 * after the radio's pre-emphasis the receiver gets a flat signal. */
flex_err_t flex_baseband_ex(const uint8_t *bits, size_t nbits,
                            flex_speed_t speed, float sample_rate,
                            int flags,
                            float *out, size_t out_cap,
                            size_t *out_len);

/* ---- Demodulator ---- */

#define FLEX_DEMOD_NPHASE 5   /* parallel phase offsets for timing recovery */

typedef struct {
	flex_speed_t speed;
	float        sample_rate;
	int          baud;

	/* Symbol timing */
	float        phase_max;     /* samples per symbol */
	int          locked;        /* 1 = best_phase selected */
	int          lock_count;    /* symbols evaluated since signal appeared */
	int          best_phase;    /* index of selected phase (0..NPHASE-1) */

	/* Multi-phase Goertzel timing recovery.
	 * Three filterbanks run at evenly-spaced phase offsets.
	 * After LOCK_THRESHOLD symbols with signal, the phase
	 * with the highest average contrast is selected and the
	 * others are retired. */
	struct {
		float gs1[4], gs2[4];   /* Goertzel accumulators */
		float phase;            /* sample-counter for this phase */
		float contrast_sum;     /* accumulated contrast */
		int   eval_count;       /* symbols evaluated */
	} ph[FLEX_DEMOD_NPHASE];

	/* Frequency discriminator (quadrature delay) */
	float        prev_i;        /* previous in-phase sample */
	float        prev_q;        /* previous quadrature sample */
	float        center_freq;   /* expected center frequency */

	/* Baseband demod state */
	float        sym_accum;
	int          sym_count;

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

/* Feed baseband (FM discriminator output) samples to the demodulator.
 * This is for post-discriminator audio from an SDR or radio receiver.
 * The signal is NRZ-like: positive levels = mark, negative = space.
 * For 2-FSK: slices at zero crossing. For 4-FSK: 4-level slicer.
 * Bits accumulate in demod->out_bits[]. */
flex_err_t flex_demod_baseband(flex_demod_t *demod,
                               const float *samples, size_t nsamples);

/* Reset demodulator state (full re-acquire) */
void flex_demod_reset(flex_demod_t *demod);

/* Soft reset — clear bit/Goertzel state but keep the locked
 * phase selection.  Use between frames within one transmission. */
void flex_demod_soft_reset(flex_demod_t *demod);

/* ---- Multi-phase FSK receiver ----
 *
 * High-level wrapper that runs FLEX_DEMOD_NPHASE demod+decoder pairs
 * at evenly-spaced symbol-clock offsets.  Whichever pair finds sync
 * first is used for the remainder of that frame.  This is the proven
 * approach for reliable decode over SDR / audio inputs where the
 * symbol clock phase is unknown.
 *
 * Usage:
 *   flex_rx_t rx;
 *   flex_rx_init(&rx, 48000.0f, my_callback, my_ctx);
 *   // in audio loop:
 *   flex_rx_feed(&rx, audio, n);       // FSK tones (Goertzel)
 *   // on squelch close:
 *   flex_rx_flush(&rx);
 *   // on squelch open:
 *   flex_rx_reset(&rx);
 */

typedef struct {
	flex_demod_t   demod[FLEX_DEMOD_NPHASE];
	flex_decoder_t decoder[FLEX_DEMOD_NPHASE];
	int            active;   /* locked phase, or -1 */
} flex_rx_t;

void       flex_rx_init(flex_rx_t *rx, float sample_rate,
                        flex_msg_cb_t cb, void *user);
void       flex_rx_reset(flex_rx_t *rx);
void       flex_rx_flush(flex_rx_t *rx);
flex_err_t flex_rx_feed(flex_rx_t *rx,
                        const float *samples, size_t nsamples);

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
