/*
 * flex_sdr — Receive and decode FLEX paging from an RTL-SDR dongle.
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz → FM discriminator → DC block → de-emphasis
 *   → decimate to 48 kHz → baseband slicer → FLEX decoder
 *
 * Usage:
 *   flex_sdr -f 929.6625
 *   flex_sdr -f 929.6625 -g 40 -i -v
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <rtl-sdr.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_SDR_RATE   240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_BUFSZ           (16384 * 4)   /* 65536 bytes = 32768 IQ samples */

static volatile int g_running = 1;

static void sighandler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* ── decoder callback ── */

static void on_message(const flex_msg_t *msg, void *user)
{
	(void)user;
	const char *type_str;
	switch (msg->type) {
	case FLEX_MSG_NUMERIC:   type_str = "numeric";   break;
	case FLEX_MSG_ALPHA:     type_str = "alpha";     break;
	case FLEX_MSG_BINARY:    type_str = "binary";    break;
	case FLEX_MSG_TONE_ONLY: type_str = "tone-only"; break;
	case FLEX_MSG_SECURE:    type_str = "secure";    break;
	default:                 type_str = "unknown";   break;
	}
	printf("[FLEX] capcode=%-7u cycle=%u frame=%-3u speed=%-4d type=%-9s",
	       (unsigned)msg->capcode, (unsigned)msg->cycle,
	       (unsigned)msg->frame, flex_speed_bps(msg->speed), type_str);

	if (msg->text_len > 0)
		printf(" msg=\"%s\"", msg->text);

	if (msg->data_len > 0) {
		printf(" data=[");
		for (size_t i = 0; i < msg->data_len; i++)
			printf("%s%02X", i ? " " : "", msg->data[i]);
		printf("]");
	}

	printf("\n");
	fflush(stdout);
}

/* ── usage ── */

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s -f <freq_mhz> [options]\n"
	    "\n"
	    "  -f freq   Frequency in MHz (required)\n"
	    "  -d index  RTL-SDR device index (default 0)\n"
	    "  -g gain   Tuner gain in dB*10 (default auto)\n"
	    "  -s rate   SDR sample rate (default %d)\n"
	    "  -r rate   Audio output rate (default %d)\n"
	    "  -i        Invert polarity\n"
	    "  -v        Verbose (print stats periodically)\n",
	    prog, DEFAULT_SDR_RATE, DEFAULT_AUDIO_RATE);
}

int main(int argc, char **argv)
{
	double    freq_mhz   = 0.0;
	int       dev_idx    = 0;
	int       gain       = -1;         /* -1 = auto */
	uint32_t  sdr_rate   = DEFAULT_SDR_RATE;
	uint32_t  audio_rate = DEFAULT_AUDIO_RATE;
	int       invert     = 0;
	int       verbose    = 0;

	int opt;
	while ((opt = getopt(argc, argv, "f:d:g:s:r:ivh")) != -1) {
		switch (opt) {
		case 'f': freq_mhz   = atof(optarg); break;
		case 'd': dev_idx    = atoi(optarg); break;
		case 'g': gain       = atoi(optarg); break;
		case 's': sdr_rate   = (uint32_t)atoi(optarg); break;
		case 'r': audio_rate = (uint32_t)atoi(optarg); break;
		case 'i': invert     = 1; break;
		case 'v': verbose    = 1; break;
		default:  usage(argv[0]); return 1;
		}
	}

	if (freq_mhz <= 0.0) {
		fprintf(stderr, "Error: frequency required (-f)\n");
		usage(argv[0]);
		return 1;
	}

	uint32_t freq_hz = (uint32_t)(freq_mhz * 1e6 + 0.5);

	/* ── set up decoder chain ── */

	flex_decoder_t decoder;
	flex_decoder_init(&decoder, on_message, NULL);

	/* Start demod at 1600 baud -- the FLEX decoder handles speed
	 * auto-detection from the sync marker; the baseband slicer
	 * just needs to be fast enough to capture 1600 baud sync. */
	flex_demod_t demod;
	flex_demod_init(&demod, (float)audio_rate);

	/* ── open RTL-SDR ── */

	int count = (int)rtlsdr_get_device_count();
	if (count == 0) {
		fprintf(stderr, "No RTL-SDR devices found\n");
		return 1;
	}
	fprintf(stderr, "Found %d RTL-SDR device(s)\n", count);

	rtlsdr_dev_t *dev = NULL;
	int rc = rtlsdr_open(&dev, (uint32_t)dev_idx);
	if (rc < 0) {
		fprintf(stderr, "Failed to open device %d (rc=%d)\n", dev_idx, rc);
		return 1;
	}

	rtlsdr_set_center_freq(dev, freq_hz);
	rtlsdr_set_sample_rate(dev, sdr_rate);

	if (gain < 0) {
		rtlsdr_set_tuner_gain_mode(dev, 0);
		fprintf(stderr, "Gain: auto\n");
	} else {
		rtlsdr_set_tuner_gain_mode(dev, 1);
		rtlsdr_set_tuner_gain(dev, gain);
		fprintf(stderr, "Gain: %.1f dB\n", gain / 10.0);
	}

	rtlsdr_reset_buffer(dev);

	fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %u Hz | FLEX%s\n",
	        freq_hz / 1e6, sdr_rate, audio_rate,
	        invert ? " | INVERTED" : "");
	fprintf(stderr, "Listening... (Ctrl-C to stop)\n\n");

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	/* ── allocate buffers ── */

	unsigned char *iq_buf = (unsigned char *)malloc(IQ_BUFSZ);
	int max_audio = (int)((IQ_BUFSZ / 2) *
	                      ((double)audio_rate / sdr_rate)) + 16;
	float *audio_buf = (float *)malloc((size_t)max_audio * sizeof(float));

	/* ── FM demod state ── */

	float prev_i = 0.0f, prev_q = 0.0f;

	/* DC blocking filter: H(z) = (1 - z^-1) / (1 - a*z^-1) */
	float dc_x_prev = 0.0f, dc_y_prev = 0.0f;
	const float alpha_dc = 0.998f;

	/* De-emphasis: 75 us time constant */
	float deemph = 0.0f;
	float alpha_de = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);

	/* Fractional decimation: sdr_rate → audio_rate */
	double dec_step = (double)audio_rate / (double)sdr_rate;
	double dec_acc = 0.0;
	float  dec_sum = 0.0f;
	int    dec_count = 0;

	/* polarity: invert flips the sign of the FM discriminator output */
	float pol = invert ? -1.0f : 1.0f;

	uint32_t total_bits = 0;

	/* ── main loop ── */

	while (g_running) {
		int n_read = 0;
		rc = rtlsdr_read_sync(dev, iq_buf, IQ_BUFSZ, &n_read);
		if (rc < 0) {
			fprintf(stderr, "rtlsdr_read_sync failed (rc=%d)\n", rc);
			break;
		}
		if (n_read == 0) continue;

		int niq = n_read / 2;
		int audio_pos = 0;

		for (int i = 0; i < niq; i++) {
			float si = ((float)iq_buf[i * 2]     - 127.5f) / 127.5f;
			float sq = ((float)iq_buf[i * 2 + 1] - 127.5f) / 127.5f;

			/* FM discriminator (atan2 of cross/dot product) */
			float dot   = si * prev_i + sq * prev_q;
			float cross = sq * prev_i - si * prev_q;
			float fm    = atan2f(cross, dot) * pol;
			prev_i = si;
			prev_q = sq;

			/* DC blocker */
			float dc_out = fm - dc_x_prev + alpha_dc * dc_y_prev;
			dc_x_prev = fm;
			dc_y_prev = dc_out;

			/* De-emphasis (75 us) */
			deemph += alpha_de * (dc_out - deemph);

			/* Averaging decimation to audio rate */
			dec_sum += deemph;
			dec_count++;
			dec_acc += dec_step;

			if (dec_acc >= 1.0) {
				dec_acc -= 1.0;
				float avg = dec_sum / (float)dec_count;
				if (audio_pos < max_audio)
					audio_buf[audio_pos++] = avg;
				dec_sum = 0.0f;
				dec_count = 0;
			}
		}

		if (audio_pos == 0) continue;

		/* feed baseband audio into FLEX baseband demodulator */
		demod.out_count = 0;
		flex_demod_baseband(&demod, audio_buf, (size_t)audio_pos);

		/* feed recovered bits into FLEX decoder */
		if (demod.out_count > 0) {
			flex_decoder_feed_bits(&decoder, demod.out_bits,
			                      demod.out_count);
			total_bits += (uint32_t)demod.out_count;
		}

		/* periodic stats */
		if (verbose && total_bits > 0 && total_bits % 10000 < 100) {
			fprintf(stderr, "[stats] bits=%u frames=%u msgs=%u "
			        "bch_ok=%u bch_err=%u\n",
			        total_bits, decoder.stat_frames,
			        decoder.stat_messages,
			        decoder.stat_corrected,
			        decoder.stat_errors);
		}
	}

	/* flush any partial frame */
	flex_decoder_flush(&decoder);

	fprintf(stderr, "\nStopped. bits=%u frames=%u messages=%u\n",
	        total_bits, decoder.stat_frames, decoder.stat_messages);

	free(audio_buf);
	free(iq_buf);
	rtlsdr_close(dev);
	return 0;
}
