#define _DEFAULT_SOURCE

/*
 * flex_tx — Transmit FLEX pages via an AIOC / CM108 USB audio device.
 *
 * Pipeline:
 *   libflex encoder → modulator (FSK audio) → PortAudio → AIOC
 *   Serial DTR PTT ──────────────────────────────────────┘
 *
 * Usage:
 *   flex_tx -D All-In-One -P /dev/ttyACM0 -c 1234567 -m "Hello"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <portaudio.h>
#include <libflex/flex.h>
#include <libflex/modem.h>

#define SAMPLE_RATE   48000
#define TX_DELAY_MS   1500    /* PTT-to-audio delay (radio ramp-up) */
#define TX_TAIL_MS    500     /* extra hold after audio drain */

/* ── PTT control ── */

/* Serial DTR PTT (e.g. /dev/ttyUSB0) */
static int serial_ptt_open(const char *path)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;

	struct termios tio;
	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &tio);

	int bits = TIOCM_DTR;
	ioctl(fd, TIOCMBIC, &bits);
	return fd;
}

static int serial_ptt_set(int fd, int on)
{
	if (fd < 0) return -1;
	int bits = TIOCM_DTR;
	return ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bits);
}

/* CM108/AIOC HID GPIO PTT (e.g. /dev/hidraw0)
 * AIOC uses GPIO2 (bit 2, 0x04) for PTT via HID output report. */
#define AIOC_PTT_GPIO  0x04    /* GPIO2 */

static int cm108_ptt_open(const char *path)
{
	return open(path, O_WRONLY);
}

static int cm108_ptt_set(int fd, int on)
{
	if (fd < 0) return -1;
	uint8_t buf[5] = {
		0x00,                                   /* HID report ID */
		0x00,                                   /* reserved */
		on ? AIOC_PTT_GPIO : 0x00,             /* GPIO direction: output / input */
		on ? AIOC_PTT_GPIO : 0x00,             /* GPIO data */
		0x00                                    /* reserved */
	};
	return write(fd, buf, sizeof(buf)) == sizeof(buf) ? 0 : -1;
}

/* auto-detect PTT type from path */
static int is_hidraw(const char *path)
{
	return strstr(path, "hidraw") != NULL;
}

static int ptt_open(const char *path)
{
	return is_hidraw(path) ? cm108_ptt_open(path) : serial_ptt_open(path);
}

static int ptt_set(const char *path, int fd, int on)
{
	return is_hidraw(path) ? cm108_ptt_set(fd, on) : serial_ptt_set(fd, on);
}

/* ── PortAudio playback ── */

static PaStream *g_stream;

static int audio_open(const char *device_name)
{
	PaError err;
	PaDeviceIndex dev = paNoDevice;
	PaStreamParameters out_params;

	err = Pa_Initialize();
	if (err != paNoError) {
		fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
		return -1;
	}

	int ndev = Pa_GetDeviceCount();
	for (int i = 0; i < ndev; i++) {
		const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
		if (info && info->maxOutputChannels > 0 &&
		    strstr(info->name, device_name)) {
			dev = i;
			break;
		}
	}
	if (dev == paNoDevice) {
		fprintf(stderr, "Audio device '%s' not found.  Available:\n", device_name);
		for (int i = 0; i < ndev; i++) {
			const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
			if (info && info->maxOutputChannels > 0)
				fprintf(stderr, "  [%d] %s\n", i, info->name);
		}
		Pa_Terminate();
		return -1;
	}

	fprintf(stderr, "Audio: %s\n", Pa_GetDeviceInfo(dev)->name);

	memset(&out_params, 0, sizeof(out_params));
	out_params.device = dev;
	out_params.channelCount = 1;
	out_params.sampleFormat = paInt16;
	out_params.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;

	err = Pa_OpenStream(&g_stream, NULL, &out_params,
	                    SAMPLE_RATE, 256, paClipOff, NULL, NULL);
	if (err != paNoError) {
		fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err));
		Pa_Terminate();
		return -1;
	}

	err = Pa_StartStream(g_stream);
	if (err != paNoError) {
		fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err));
		Pa_CloseStream(g_stream); g_stream = NULL;
		Pa_Terminate();
		return -1;
	}
	return 0;
}

static int audio_write(const int16_t *samples, size_t nsamples)
{
	PaError err = Pa_WriteStream(g_stream, samples, (unsigned long)nsamples);
	if (err != paNoError && err != paOutputUnderflowed) {
		fprintf(stderr, "Pa_WriteStream: %s\n", Pa_GetErrorText(err));
		return -1;
	}
	return 0;
}

static void audio_close(void)
{
	if (g_stream) {
		Pa_StopStream(g_stream);
		Pa_CloseStream(g_stream);
		g_stream = NULL;
	}
	Pa_Terminate();
}

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s [options] -c <capcode> [-m <message>]\n\n"
	    "  -c capcode    Pager address (required)\n"
	    "  -m message    Alphanumeric message text\n"
	    "  -n digits     Numeric message\n"
	    "  -t            Tone-only page\n"
	    "  -s speed      1600 (default), 3200, 3200-4, 6400\n"
	    "  -D device     PortAudio device name substring (default: All-In-One)\n"
	    "  -P port       PTT device: /dev/hidrawN for AIOC/CM108,\n"
	    "                /dev/ttyXXX for serial DTR (default: /dev/hidraw1)\n"
	    "  -l level      Audio level 0.0-1.0 (default: 0.15)\n"
	    "  -d ms         TX delay in ms (default: %d)\n"
	    "  -v            Verbose\n",
	    prog, TX_DELAY_MS);
}

int main(int argc, char **argv)
{
	uint32_t capcode = 0;
	int      have_cap = 0;
	const char *alpha_msg = NULL;
	const char *num_msg = NULL;
	int      tone_only = 0;
	flex_speed_t speed = FLEX_SPEED_1600_2;
	const char *audio_dev = "All-In-One";
	const char *ptt_port = "/dev/hidraw1";
	float    level = 0.15f;
	int      tx_delay = TX_DELAY_MS;
	int      verbose = 0;

	int opt;
	while ((opt = getopt(argc, argv, "c:m:n:ts:D:P:l:d:vh")) != -1) {
		switch (opt) {
		case 'c': capcode = (uint32_t)strtoul(optarg, NULL, 10); have_cap = 1; break;
		case 'm': alpha_msg = optarg; break;
		case 'n': num_msg = optarg; break;
		case 't': tone_only = 1; break;
		case 's':
			if (strcmp(optarg, "3200") == 0) speed = FLEX_SPEED_3200_2;
			else if (strcmp(optarg, "3200-4") == 0) speed = FLEX_SPEED_3200_4;
			else if (strcmp(optarg, "6400") == 0) speed = FLEX_SPEED_6400_4;
			else speed = FLEX_SPEED_1600_2;
			break;
		case 'D': audio_dev = optarg; break;
		case 'P': ptt_port = optarg; break;
		case 'l': level = (float)atof(optarg); break;
		case 'd': tx_delay = atoi(optarg); break;
		case 'v': verbose = 1; break;
		default:  usage(argv[0]); return 1;
		}
	}

	if (!have_cap) {
		fprintf(stderr, "Error: capcode required (-c)\n");
		usage(argv[0]); return 1;
	}
	if (!alpha_msg && !num_msg && !tone_only) {
		fprintf(stderr, "Error: specify -m, -n, or -t\n");
		usage(argv[0]); return 1;
	}
	if (level <= 0.0f || level > 1.0f) level = 0.5f;

	/* ── encode ── */
	flex_msg_type_t type;
	const char *text = NULL;

	if (tone_only)      { type = FLEX_MSG_TONE_ONLY; }
	else if (num_msg)   { type = FLEX_MSG_NUMERIC; text = num_msg; }
	else                { type = FLEX_MSG_ALPHA;   text = alpha_msg; }

	uint8_t bitbuf[FLEX_BITSTREAM_MAX];
	size_t bs_len, bs_bits;
	flex_err_t err = flex_encode_single(capcode, FLEX_ADDR_SHORT, type, speed,
	                                     text, NULL, 0,
	                                     bitbuf, sizeof(bitbuf),
	                                     &bs_len, &bs_bits);
	if (err != FLEX_OK) {
		fprintf(stderr, "Encode failed: %s\n", flex_strerror(err));
		return 1;
	}
	if (verbose)
		fprintf(stderr, "Encoded: %zu bits (%zu bytes)\n", bs_bits, bs_len);

	/* unpack MSB-first packed bytes to bit array */
	uint8_t *unpacked = (uint8_t *)malloc(bs_bits);
	if (!unpacked) { fprintf(stderr, "Out of memory\n"); return 1; }
	for (size_t i = 0; i < bs_bits; i++)
		unpacked[i] = (bitbuf[i / 8] >> (7 - (i % 8))) & 1;

	/* ── modulate ── */
	flex_mod_t mod;
	flex_mod_init(&mod, speed, (float)SAMPLE_RATE);

	size_t max_samples = (bs_bits * (size_t)((float)SAMPLE_RATE / 1600.0f + 1)) + 1024;
	float *fsamples = (float *)malloc(max_samples * sizeof(float));
	if (!fsamples) { free(unpacked); fprintf(stderr, "Out of memory\n"); return 1; }

	size_t nsamples;
	err = flex_mod_bits(&mod, unpacked, bs_bits,
	                     fsamples, max_samples, &nsamples);
	free(unpacked);
	if (err != FLEX_OK) {
		fprintf(stderr, "Modulate failed: %s\n", flex_strerror(err));
		free(fsamples);
		return 1;
	}
	if (verbose)
		fprintf(stderr, "Modulated: %zu samples (%.2f sec) at %d Hz\n",
		        nsamples, (double)nsamples / SAMPLE_RATE, SAMPLE_RATE);

	/* convert float [-1,+1] to int16 with level scaling */
	int16_t *pcm = (int16_t *)malloc(nsamples * sizeof(int16_t));
	if (!pcm) { free(fsamples); fprintf(stderr, "Out of memory\n"); return 1; }
	for (size_t i = 0; i < nsamples; i++) {
		float s = fsamples[i] * level * 32000.0f;
		if (s > 32767.0f) s = 32767.0f;
		if (s < -32768.0f) s = -32768.0f;
		pcm[i] = (int16_t)s;
	}
	free(fsamples);

	/* ── open audio first (slow — scans ALSA/JACK) ── */
	if (audio_open(audio_dev) < 0) {
		free(pcm);
		return 1;
	}

	/* ── PTT on ── */
	int ptt_fd = ptt_open(ptt_port);
	if (ptt_fd < 0) {
		fprintf(stderr, "Warning: cannot open %s for PTT (running without PTT)\n",
		        ptt_port);
	}

	if (verbose) fprintf(stderr, "PTT ON (%s)\n",
	                     is_hidraw(ptt_port) ? "HID GPIO" : "serial DTR");
	ptt_set(ptt_port, ptt_fd, 1);
	usleep((unsigned)(tx_delay * 1000));

	/* ── play audio ── */
	if (verbose) fprintf(stderr, "Transmitting...\n");
	int rc = audio_write(pcm, nsamples);

	/* drain DAC buffer before dropping PTT */
	audio_close();
	usleep(TX_TAIL_MS * 1000);

	/* ── PTT off ── */
	ptt_set(ptt_port, ptt_fd, 0);
	if (verbose) fprintf(stderr, "PTT OFF\n");

	if (ptt_fd >= 0) close(ptt_fd);
	free(pcm);

	if (rc == 0) {
		fprintf(stderr, "Transmitted: capcode=%u speed=%d %s\n",
		        capcode, flex_speed_bps(speed),
		        tone_only ? "(tone)" : text);
	}
	return rc;
}
