/*
 * flex_sdr — Receive and decode FLEX paging from an RTL-SDR dongle.
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz → FM discriminator → DC block → de-emphasis
 *   → decimate to 48 kHz → squelch gate → FSK/baseband demod → FLEX decoder
 *
 * Usage:
 *   flex_sdr -f 929.6625
 *   flex_sdr -f 462.550 -g 40 -v -m fsk
 *   flex_sdr -f 462.550 -g 40 -v -S 12000:14000 -w capture.wav
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <rtl-sdr.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_SDR_RATE   240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_BUFSZ           (16384 * 4)   /* 65536 bytes = 32768 IQ samples */

/* Squelch defaults (FM-inverted: low RMS = signal present) */
#define DEFAULT_SQ_OPEN    15000
#define DEFAULT_SQ_CLOSE   17000
#define SQ_DEBOUNCE_OPEN   3
#define SQ_DEBOUNCE_CLOSE  5

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
	    "  -f freq       Frequency in MHz (required)\n"
	    "  -d index      RTL-SDR device index (default 0)\n"
	    "  -g gain       Tuner gain in dB*10 (default auto)\n"
	    "  -s rate       SDR sample rate (default %d)\n"
	    "  -r rate       Audio output rate (default %d)\n"
	    "  -S open:close Squelch thresholds (default %d:%d)\n"
	    "  -m mode       Demod mode: fsk (default) or baseband\n"
	    "  -w file       Dump raw audio to 16-bit WAV for analysis\n"
	    "  -E            Disable de-emphasis (for data/FSK signals)\n"
	    "  -i            Invert polarity\n"
	    "  -v            Verbose (print stats periodically)\n",
	    prog, DEFAULT_SDR_RATE, DEFAULT_AUDIO_RATE,
	    DEFAULT_SQ_OPEN, DEFAULT_SQ_CLOSE);
}

int main(int argc, char **argv)
{
	double    freq_mhz    = 0.0;
	int       dev_idx     = 0;
	int       gain        = -1;         /* -1 = auto */
	uint32_t  sdr_rate    = DEFAULT_SDR_RATE;
	uint32_t  audio_rate  = DEFAULT_AUDIO_RATE;
	int       sq_open_th  = DEFAULT_SQ_OPEN;
	int       sq_close_th = DEFAULT_SQ_CLOSE;
	int       use_fsk     = 1;          /* default: FSK Goertzel */
	const char *wav_path  = NULL;
	int       no_deemph   = 0;
	int       invert      = 0;
	int       verbose     = 0;

	int opt;
	while ((opt = getopt(argc, argv, "f:d:g:s:r:S:m:w:Eivh")) != -1) {
		switch (opt) {
		case 'f': freq_mhz    = atof(optarg); break;
		case 'd': dev_idx     = atoi(optarg); break;
		case 'g': gain        = atoi(optarg); break;
		case 's': sdr_rate    = (uint32_t)atoi(optarg); break;
		case 'r': audio_rate  = (uint32_t)atoi(optarg); break;
		case 'S': sscanf(optarg, "%d:%d", &sq_open_th, &sq_close_th); break;
		case 'm': use_fsk     = (strcmp(optarg, "baseband") != 0); break;
		case 'w': wav_path    = optarg; break;
		case 'E': no_deemph   = 1; break;
		case 'i': invert      = 1; break;
		case 'v': verbose     = 1; break;
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

	fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %u Hz | FLEX | %s%s\n",
	        freq_hz / 1e6, sdr_rate, audio_rate,
	        use_fsk ? "FSK" : "baseband",
	        invert ? " | INVERTED" : "");
	fprintf(stderr, "Squelch: open=%d close=%d (FM-inverted RMS)\n",
	        sq_open_th, sq_close_th);
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
	float dc_x_prev = 0.0f, dc_y_prev = 0.0f;
	const float alpha_dc = 0.998f;
	float deemph_state = 0.0f;
	float alpha_de = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);
	double dec_step = (double)audio_rate / (double)sdr_rate;
	double dec_acc = 0.0;
	float  dec_sum = 0.0f;
	int    dec_count = 0;
	float  pol = invert ? -1.0f : 1.0f;

	/* ── WAV dump ── */

	FILE *wav_fp = NULL;
	uint32_t wav_samples = 0;

	if (wav_path) {
		wav_fp = fopen(wav_path, "wb");
		if (!wav_fp) {
			perror(wav_path);
			rtlsdr_close(dev);
			return 1;
		}
		uint8_t hdr[44] = {0};
		memcpy(hdr, "RIFF", 4);
		memcpy(hdr + 8, "WAVEfmt ", 8);
		uint32_t v; uint16_t s16;
		v = 16;         memcpy(hdr + 16, &v, 4);
		s16 = 1;        memcpy(hdr + 20, &s16, 2);
		s16 = 1;        memcpy(hdr + 22, &s16, 2);
		v = audio_rate; memcpy(hdr + 24, &v, 4);
		v = audio_rate*2; memcpy(hdr + 28, &v, 4);
		s16 = 2;        memcpy(hdr + 32, &s16, 2);
		s16 = 16;       memcpy(hdr + 34, &s16, 2);
		memcpy(hdr + 36, "data", 4);
		fwrite(hdr, 1, 44, wav_fp);
		fprintf(stderr, "Dumping audio to %s\n", wav_path);
	}

	/* ── squelch state ── */

	int sq_open = 0;
	int sq_open_cnt = 0, sq_close_cnt = 0;
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

			float dot   = si * prev_i + sq * prev_q;
			float cross = sq * prev_i - si * prev_q;
			float fm    = atan2f(cross, dot) * pol;
			prev_i = si;
			prev_q = sq;

			float dc_out = fm - dc_x_prev + alpha_dc * dc_y_prev;
			dc_x_prev = fm;
			dc_y_prev = dc_out;

			if (!no_deemph)
				deemph_state += alpha_de * (dc_out - deemph_state);

			dec_sum += no_deemph ? dc_out : deemph_state;
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

		/* ── WAV dump (all audio, pre-squelch) ── */

		if (wav_fp) {
			for (int j = 0; j < audio_pos; j++) {
				float s = audio_buf[j] * 16000.0f;
				if (s > 32767.0f) s = 32767.0f;
				if (s < -32768.0f) s = -32768.0f;
				int16_t pcm = (int16_t)s;
				fwrite(&pcm, 2, 1, wav_fp);
				wav_samples++;
			}
		}

		/* ── squelch ── */

		int64_t rms_sum = 0;
		for (int j = 0; j < audio_pos; j++) {
			int32_t v = (int32_t)(audio_buf[j] * 16000.0f);
			rms_sum += (int64_t)v * v;
		}
		int32_t rms = (int32_t)sqrtf((float)(rms_sum / audio_pos));

		if (!sq_open) {
			if (rms < sq_open_th) {
				sq_open_cnt++;
				if (sq_open_cnt >= SQ_DEBOUNCE_OPEN) {
					sq_open = 1;
					sq_close_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] OPEN  rms=%d\n",
						        rms);
					flex_decoder_reset(&decoder);
					flex_demod_reset(&demod);
				}
			} else {
				sq_open_cnt = 0;
			}
		} else {
			if (rms > sq_close_th) {
				sq_close_cnt++;
				if (sq_close_cnt >= SQ_DEBOUNCE_CLOSE) {
					sq_open = 0;
					sq_open_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] CLOSE rms=%d\n",
						        rms);
					flex_decoder_flush(&decoder);
				}
			} else {
				sq_close_cnt = 0;
			}
		}

		/* ── feed audio when squelch is open ── */

		if (sq_open) {
			demod.out_count = 0;

			if (use_fsk)
				flex_demod_feed(&demod, audio_buf,
				                (size_t)audio_pos);
			else
				flex_demod_baseband(&demod, audio_buf,
				                    (size_t)audio_pos);

			if (demod.out_count > 0) {
				flex_decoder_feed_bits(&decoder, demod.out_bits,
				                      demod.out_count);
				total_bits += (uint32_t)demod.out_count;
			}
		}

		/* periodic stats */
		if (verbose && total_bits > 0 && total_bits % 5000 < 100) {
			fprintf(stderr, "[stats] bits=%u frames=%u msgs=%u "
			        "bch_ok=%u bch_err=%u rms=%d sq=%s\n",
			        total_bits, decoder.stat_frames,
			        decoder.stat_messages,
			        decoder.stat_corrected,
			        decoder.stat_errors,
			        rms, sq_open ? "OPEN" : "closed");
		}
	}

	flex_decoder_flush(&decoder);

	fprintf(stderr, "\nStopped. bits=%u frames=%u messages=%u\n",
	        total_bits, decoder.stat_frames, decoder.stat_messages);

	/* fix WAV header */
	if (wav_fp) {
		uint32_t data_bytes = wav_samples * 2;
		uint32_t file_size  = 36 + data_bytes;
		fseek(wav_fp, 4, SEEK_SET);
		fwrite(&file_size, 4, 1, wav_fp);
		fseek(wav_fp, 40, SEEK_SET);
		fwrite(&data_bytes, 4, 1, wav_fp);
		fclose(wav_fp);
		fprintf(stderr, "Wrote %u samples to WAV\n", wav_samples);
	}

	free(audio_buf);
	free(iq_buf);
	rtlsdr_close(dev);
	return 0;
}
