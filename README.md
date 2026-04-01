# libflex

C library for **FLEX** (Motorola) paging protocol encoding, decoding, and FSK modulation/demodulation. Generates and parses complete FLEX frame bitstreams with sync detection, BCH error correction (2-bit), block interleaving, multi-phase support, multi-speed operation (1600/3200/6400 bps), Goertzel-based 2-FSK/4-FSK modem with WAV file I/O, and NRZ baseband output compatible with multimon-ng.

C99, no external dependencies beyond libc and libm. All structs are stack-allocatable with zero dynamic memory allocation. Thread-safe (no mutable global state).

Cross-validated against [multimon-ng](https://github.com/EliasOenal/multimon-ng) -- encoded frames are decoded correctly by standard FLEX receivers.

## Table of Contents

- [Features](#features)
- [Building](#building)
- [API](#api)
  - [Single Message Encoding](#single-message-encoding)
  - [Batch Encoding](#batch-encoding)
  - [Streaming Decoder](#streaming-decoder)
  - [Baseband Output](#baseband-output)
  - [FSK Modem](#fsk-modem)
  - [BCH Error Correction](#bch-error-correction)
  - [Error Codes](#error-codes)
- [Protocol Details](#protocol-details)
- [Test Suite](#test-suite)
- [Example Programs](#example-programs)
- [Project Structure](#project-structure)
- [Technical Details](#technical-details)
- [License](#license)

## Features

- **FLEX Encoder** -- Single or batch message encoding to complete FLEX frame bitstreams with preamble, inverted Sync1, dotting, LSB-first FIW, Sync2, block interleaving, message headers, and multi-phase support at all four speeds
- **FLEX Decoder** -- Streaming 6-state state-machine decoder with inverted sync marker detection, speed auto-detection, LSB-first FIW decode, dotting skip, callback-driven message delivery, and bit/byte/symbol input
- **Baseband Output** -- Built-in NRZ/4-level baseband sample generator (`flex_baseband`) with fixed-point timing matching multimon-ng's generator for direct FM transmitter feeding
- **FSK Modem** -- Goertzel-based 2-FSK and 4-FSK modulator/demodulator with adaptive baseband frequencies, WAV file read/write, and support for 8/16/32/48 kHz sample rates
- **BCH(31,21) Codec** -- Full encode, syndrome check, 1-bit and 2-bit error correction via precomputed compile-time syndrome table (528 entries, fully thread-safe). Returns correction count (0, 1, 2, or -1 for uncorrectable)
- **Block Interleaving** -- 8x32 matrix interleave/deinterleave (LSB-first column order); 16-bit burst errors fully correctable after deinterleaving
- **Multi-Speed** -- 1600 bps (2-FSK), 3200 bps (2-FSK or 4-FSK), 6400 bps (4-FSK) with automatic multi-phase encoding/decoding
- **Numeric Messages** -- BCD encoding/decoding with digits 0-9 and special characters `*U -[]()`
- **Alphanumeric Messages** -- 7-bit ASCII packing (3 characters per 21-bit codeword data field) with 7-bit skip on first content word per FLEX spec
- **Binary Messages** -- Arbitrary binary data with configurable bit width (1-16 bits per item)
- **Tone-Only Pages** -- Address-only pages with no message data
- **Zero Allocation** -- All encoder, decoder, modulator, and demodulator contexts are fixed-size structs, stack-allocatable with no malloc
- **Thread-Safe** -- No mutable global state; BCH syndrome table is `static const`

### Supported Parameters

| Parameter | Values |
|-----------|--------|
| Speeds | 1600/2, 3200/2, 3200/4, 6400/4 bps |
| Phases | 1 (1600/2), 2 (3200/2, 3200/4), 4 (6400/4) |
| Message types | Numeric, alphanumeric, binary, tone-only, secure |
| Addresses | Short (single codeword) and long (two codewords) |
| BCH correction | 1-bit and 2-bit correction, 3+ bit detection |
| Interleaving | 8x32 block interleave per block per phase (LSB-first) |
| Messages per frame | Up to 32 queued |
| Modem sample rates | 8000, 16000, 32000, 48000 Hz |
| Modulation | 2-FSK (1600/3200 baud), 4-FSK (1600/3200 baud) |

### Modem Sample Rate Support

| Sample Rate | 1600/2 | 3200/2 | 3200/4 | 6400/4 |
|-------------|--------|--------|--------|--------|
| 48 kHz | yes | yes | yes | yes |
| 32 kHz | yes | yes | yes | yes |
| 16 kHz | yes | yes | yes | -- |
| 8 kHz | yes | -- | -- | -- |

Higher FLEX speeds require higher sample rates for the Goertzel demodulator to resolve tone frequencies reliably (minimum ~8 samples per symbol).

## Building

```bash
./autogen.sh    # generate configure (requires autoconf, automake, libtool)
./configure
make            # builds libflex.so, libflex.a, and example programs
make check      # runs the test suite (131 tests)
make install    # installs library, headers, and pkg-config file
```

Compiles with `-Wall -Wextra -pedantic` in C99 strict mode. Requires a C99 compiler, libc, and libm (for FSK modem sinf/cosf/atan2f).

### Debian Packages

```bash
dpkg-buildpackage -us -uc -b
```

Produces `libflex0` (shared library), `libflex-dev` (headers + pkg-config), and `libflex0-dbgsym` (debug symbols).

### CI

GitHub Actions workflows are included:
- **CI** (`ci.yml`): builds, tests, and packages on every push/PR to main
- **Release** (`release.yml`): triggered on version tags (`v*`), builds `.deb` packages for amd64 and arm64 across Ubuntu 24.04, Debian 12, and Debian 13

## API

All public functions return `flex_err_t` (0 on success). Headers are under `include/libflex/`, included via `<libflex/flex.h>`.

### Single Message Encoding

```c
#include <libflex/flex.h>

uint8_t buf[FLEX_BITSTREAM_MAX];
size_t len, bits;

/* Encode a numeric page at 1600 bps */
flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_NUMERIC,
                   FLEX_SPEED_1600_2, "5551234", NULL, 0,
                   buf, sizeof(buf), &len, &bits);
/* buf now contains a complete FLEX frame bitstream */
/* len = bytes written, bits = exact bit count */

/* Encode an alphanumeric page at 6400 bps */
flex_encode_single(200000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
                   FLEX_SPEED_6400_4, "Hello FLEX!", NULL, 0,
                   buf, sizeof(buf), &len, &bits);

/* Encode a tone-only page */
flex_encode_single(500, FLEX_ADDR_SHORT, FLEX_MSG_TONE_ONLY,
                   FLEX_SPEED_1600_2, NULL, NULL, 0,
                   buf, sizeof(buf), &len, &bits);

/* Encode a binary data page */
uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
flex_encode_single(300000, FLEX_ADDR_SHORT, FLEX_MSG_BINARY,
                   FLEX_SPEED_3200_2, NULL, data, 4,
                   buf, sizeof(buf), &len, &bits);
```

### Batch Encoding

Queue multiple messages for packing into a single FLEX frame:

```c
flex_encoder_t enc;
flex_encoder_init(&enc, FLEX_SPEED_3200_2);
flex_encoder_set_frame(&enc, 5, 64);  /* cycle 5, frame 64 */

/* Queue messages for different pagers */
flex_encoder_add(&enc, 100000, FLEX_ADDR_SHORT,
                 FLEX_MSG_NUMERIC, "5551234", NULL, 0);
flex_encoder_add(&enc, 200000, FLEX_ADDR_SHORT,
                 FLEX_MSG_ALPHA, "Meeting at 3pm", NULL, 0);
flex_encoder_add(&enc, 300000, FLEX_ADDR_SHORT,
                 FLEX_MSG_TONE_ONLY, NULL, NULL, 0);

/* Encode all messages into a single frame bitstream */
uint8_t buf[FLEX_BITSTREAM_MAX];
size_t len, bits;
flex_encode(&enc, buf, sizeof(buf), &len, &bits);
```

### Streaming Decoder

Feed bits or bytes incrementally. The callback fires for each complete message:

```c
void on_message(const flex_msg_t *msg, void *user)
{
    printf("capcode=%u type=%d speed=%d msg=\"%s\"\n",
           msg->capcode, msg->type,
           flex_speed_bps(msg->speed), msg->text);
}

flex_decoder_t dec;
flex_decoder_init(&dec, on_message, NULL);

/* Feed packed bytes (MSB-first, e.g., from a file or demodulator) */
flex_decoder_feed_bytes(&dec, data, nbytes);

/* Or feed individual bits (one bit per byte, 0 or 1) */
flex_decoder_feed_bits(&dec, bit_array, nbits);

/* Flush any partially assembled frame at end of stream */
flex_decoder_flush(&dec);

/* Check statistics */
printf("frames=%u codewords=%u corrected=%u errors=%u messages=%u\n",
       dec.stat_frames, dec.stat_codewords, dec.stat_corrected,
       dec.stat_errors, dec.stat_messages);
```

The decoder handles:
- Inverted sync marker detection (`~0xA6C6AAAA`) from arbitrary bit positions
- Speed auto-detection from 16-bit mode word (with Hamming distance tolerance)
- 16-bit dotting skip between Sync1 and FIW
- LSB-first FIW decode for cycle/frame metadata
- Sync2 dotting skip at the correct symbol baud rate
- BCH 2-bit error correction on every codeword
- LSB-first block deinterleaving at all speeds
- Multi-phase data separation (1/2/4 phases)
- Message header parsing (fragment/continuation flags)
- 7-bit alpha skip on first content word
- Address/vector/message field reassembly after full frame decode
- FLEX spec page type mapping (0=secure, 2=tone, 3/4/7=numeric, 5=alpha, 6=binary)

### Baseband Output

Generate NRZ/4-level baseband samples for direct FM transmitter feeding or multimon-ng compatibility:

```c
#include <libflex/modem.h>

uint8_t buf[FLEX_BITSTREAM_MAX];
size_t len, bits;

/* Encode a message */
flex_encode_single(100000, FLEX_ADDR_SHORT, FLEX_MSG_ALPHA,
                   FLEX_SPEED_1600_2, "Hello", NULL, 0,
                   buf, sizeof(buf), &len, &bits);

/* Generate baseband samples */
float samples[200000];
size_t nsamples;
flex_baseband(buf, bits, FLEX_SPEED_1600_2, 48000.0f,
              samples, 200000, &nsamples);

/* Write to WAV for multimon-ng: multimon-ng -t wav -a FLEX page.wav */
flex_wav_write("page.wav", 48000.0f, samples, nsamples);
```

The baseband generator:
- Handles baud rate transitions (1600 baud header, mode baud data)
- Uses 16-bit fixed-point phase accumulation matching multimon-ng's timing
- 2-FSK modes: bit 1 = +0.73, bit 0 = -0.73
- 4-FSK modes: Gray-coded 4-level symbols (+-0.73, +-0.24)

### FSK Modem

Modulate FLEX bitstreams to FSK audio tones and demodulate back:

```c
#include <libflex/modem.h>

/* --- Modulate: bits to audio --- */
flex_mod_t mod;
flex_mod_init(&mod, FLEX_SPEED_1600_2, 48000.0f);

float samples[100000];
size_t nsamples;
flex_mod_bits(&mod, bits, nbits, samples, 100000, &nsamples);

/* --- Write WAV file --- */
flex_wav_write("page.wav", 48000.0f, samples, nsamples);

/* --- Read WAV file --- */
float read_buf[100000];
size_t nread;
float sr;
flex_wav_read("page.wav", read_buf, 100000, &nread, &sr);

/* --- Demodulate: audio to bits --- */
flex_demod_t demod;
flex_demod_init_speed(&demod, sr, FLEX_SPEED_1600_2);
flex_demod_feed(&demod, read_buf, nread);
/* Recovered bits in demod.out_bits[0..demod.out_count-1] */

/* --- Feed to decoder --- */
flex_decoder_t dec;
flex_decoder_init(&dec, on_message, NULL);
flex_decoder_feed_bits(&dec, demod.out_bits, demod.out_count);
flex_decoder_flush(&dec);
```

The modem uses:
- **Goertzel algorithm** for efficient single-frequency DFT energy detection
- **Adaptive baseband frequencies** scaled to the sample rate (all tones stay within Nyquist)
- **Continuous-phase FSK** modulation with precise symbol-clock phase accumulator
- **2-FSK** for 1600/2 and 3200/2 modes, **4-FSK** for 3200/4 and 6400/4 modes
- **16-bit PCM WAV** file read/write with error checking

### BCH Error Correction

The BCH(31,21) codec is exposed for direct codeword manipulation:

```c
#include <libflex/bch.h>

/* Build a valid 32-bit codeword from 21 data bits (LSB layout) */
uint32_t cw = flex_codeword_build(data21);

/* Check a codeword (returns 0 if valid) */
uint32_t syndrome = flex_bch_syndrome(cw);

/* Attempt up to 2-bit error correction
 * Returns: 0 = no errors, 1 = 1-bit corrected, 2 = 2-bit corrected, -1 = uncorrectable */
int rc = flex_bch_correct(&cw);

/* Verify even parity (returns 1 if valid) */
int ok = flex_parity_check(cw);
```

### Error Codes

| Code | Constant | Meaning |
|------|----------|---------|
| 0 | `FLEX_OK` | Success |
| -1 | `FLEX_ERR_PARAM` | Invalid argument or NULL pointer |
| -2 | `FLEX_ERR_OVERFLOW` | Output buffer too small / write error |
| -3 | `FLEX_ERR_BADCHAR` | Message contains invalid character for encoding |
| -4 | `FLEX_ERR_BCH` | Uncorrectable BCH error (3+ bits) |
| -5 | `FLEX_ERR_SYNC` | Sync word not found or lost |
| -6 | `FLEX_ERR_STATE` | Invalid state for operation |
| -7 | `FLEX_ERR_FIW` | Invalid Frame Info Word |
| -8 | `FLEX_ERR_INTERLEAVE` | Deinterleave failure |
| -9 | `FLEX_ERR_SPEED` | Unrecognized speed mode |
| -10 | `FLEX_ERR_PHASE` | Phase error |
| -11 | `FLEX_ERR_FRAGMENT` | Fragment reassembly failure |

## Protocol Details

FLEX is Motorola's one-way digital paging protocol developed in the mid-1990s. It supports data rates up to 6400 bps with multi-phase transmission, block interleaving, and synchronous frame timing (15 cycles/hour, 128 frames/cycle).

| Parameter | Value |
|-----------|-------|
| Data rates | 1600, 3200, 6400 bps |
| Modulation | 2-FSK (1600/3200 baud) and 4-FSK (1600/3200 baud) |
| Sync marker | `0xA6C6AAAA` (transmitted inverted, always at 1600 bps) |
| Mode words | `0x870C` (1600/2), `0x7B18` (3200/2), `0xB068` (1600/4), `0xDEA0` (3200/4) |
| Timing | 15 cycles/hour, 128 frames/cycle, 1.875s per frame |
| Frame structure | Preamble + Sync1 + dotting + FIW + Sync2 + 11 data blocks |
| Block size | 8 interleaved codewords per phase |
| Codeword | 32 bits: 21 data (LSB) + 10 BCH + 1 parity |
| Error correction | BCH(31,21) with generator polynomial 0x769, corrects up to 2 bits |
| Interleaving | 8x32 matrix, LSB-first column-wise transmission |
| Page types | 0=secure, 2=tone, 3=standard numeric, 5=alphanumeric, 6=binary |

### Bitstream Structure

```
[  Preamble: 960 alternating bits (0,1,0,1,...)             ]
[  Sync1 (inverted): mode word + marker + ~mode word        ]  (64 bits, 1600 bps)
[  Dotting: 16 alternating bits                              ]  (16 bits, 1600 bps)
[  FIW: 32-bit BCH-protected Frame Info Word (LSB-first)     ]  (32 bits, 1600 bps)
[  Sync2 dotting: alternating bits at symbol baud rate        ]  (25ms worth)
[  Block 0: 8 interleaved codewords x nphases                ]  (256-1024 bits)
[  Block 1: ...                                               ]
[  ...                                                        ]
[  Block 10: ...                                              ]
[  Trailing idle: 64 alternating bits                         ]
```

### Data Block Layout (Phase A)

```
Word 0:       BIW (Block Information Word)
Words 1..Na:  Address codewords (short: 1 cw, long: 2 cw)
Words Na..Nv: Vector codewords (1:1 with addresses, encode page type/location/length)
Words Nv..87: Message data (header word + content words per message)
Unused:       Alternating idle patterns for PLL lock
```

### Codeword Format (LSB layout)

```
All codewords (32 bits):
  bits 0-20  = 21 data bits
  bits 21-30 = BCH(31,21) parity (10 bits)
  bit  31    = even parity

Address codeword (short):
  data21 = capcode + 32768

Vector codeword:
  bits 4-6   = page type (FLEX spec: 0=secure, 2=tone, 3=numeric, 5=alpha, 6=binary)
  bits 7-13  = message start word index (0-127)
  bits 14-20 = message word count (0-127)

Message data:
  word 0     = header (frag=3 for complete message)
  words 1+   = content (alpha: 7-bit skip on first word, 3 chars per 21-bit word)
```

## Test Suite

```
$ make check

libflex test suite

BCH:              12 tests -- encode, syndrome, 1/2-bit correction, 3-bit detection
Interleave:        6 tests -- roundtrip, column order, burst error correction
Sync:             11 tests -- speed detection (1600/2, 3200/2, 1600/4, 3200/4), fuzzy match
FIW:               7 tests -- encode/decode roundtrip, BCH correction
BIW:               6 tests -- encode/decode roundtrip, BCH correction
Numeric:           6 tests -- BCD roundtrip, special chars, padding
Alpha:             5 tests -- 7-bit ASCII roundtrip, chunk count
Binary:            6 tests -- multi-width roundtrip (1/7/8/16 bit)
Codeword:         10 tests -- short/long address, vector page types, data codewords
Phase:             7 tests -- speed helpers, phase separation/combination
Encoder:          14 tests -- all types, all speeds, sync/FIW/BIW/BCH validation
Decoder:          12 tests -- all types at all speeds, multi-message, stats, reset
Roundtrip:        10 tests -- full encode-decode at all speeds, multi-message, edge cases
Modem:            19 tests -- modulator, WAV I/O, Goertzel demod roundtrip at
                              4 FLEX speeds x 4 sample rates (48k/32k/16k/8k)

131 passed, 0 failed
```

Tests cover:
- BCH(31,21) encode/syndrome/correct for all 31 bit positions, 2-bit correction, 3-bit detection
- 8x32 block interleave/deinterleave round-trips with burst error correction verification
- Sync marker detection at all 4 speeds with Hamming-distance fuzzy matching
- FIW and BIW encode/decode round-trips with BCH error correction
- Numeric BCD, 7-bit alpha, and binary encoding/decoding round-trips
- Short/long address, vector page type mapping, and data codeword construction
- Multi-phase separation and combination
- Encoder output validation: inverted sync position, LSB-first FIW, BIW field boundaries, BCH validity on all 88 codewords
- Streaming decoder at all 4 speeds with multi-message, statistics, and reset
- Full encode-then-decode round-trips for numeric, alpha, binary, and tone at all speeds
- FSK modulator/demodulator round-trip through audio at 48/32/16/8 kHz sample rates
- WAV file write/read round-trip with 16-bit PCM quantization verification

## Example Programs

| Program | Description |
|---------|-------------|
| `encode_page` | Encode a pager message, write raw FLEX frame bitstream to stdout |
| `decode_stream` | Read raw FLEX bitstream from stdin, print decoded messages |
| `encode_wav` | Encode a pager message to a WAV audio file (FSK tones) |
| `decode_wav` | Decode a FLEX WAV audio file back to messages |
| `gen_baseband` | Generate NRZ baseband WAV files for multimon-ng testing |
| `gen_samples` | Generate sample WAV files for all type/speed/rate combinations |
| `flex_sdr` | Live FLEX receiver using RTL-SDR (requires librtlsdr) |

```bash
# Encode a numeric page to raw bitstream
./encode_page 1234 n "5551234" > page.bin

# Encode an alpha page at 6400 bps
./encode_page 2000 a 6400 "Hello FLEX" > page.bin

# Decode a raw bitstream
./decode_stream < page.bin
# [NUM] capcode=1234 cycle=0 frame=0 speed=1600 msg="5551234"

# Pipe encode directly to decode
./encode_page 1234 n "5551234" | ./decode_stream

# Encode to WAV audio file (FSK tones)
./encode_wav 1234 n "5551234" -o page.wav

# Decode from WAV audio file
./decode_wav page.wav
# [NUM] capcode=1234 cycle=0 frame=0 speed=1600 msg="5551234"

# Generate NRZ baseband for multimon-ng
./gen_baseband 1234 n "5551234" 48000 page_baseband.wav
# Decode with: multimon-ng -t wav -a FLEX page_baseband.wav

# Generate all sample WAV files (48 files across all speeds/rates/types)
./gen_samples samples/
```

### RTL-SDR Receiver

`flex_sdr` is a live FLEX pager receiver using an RTL-SDR dongle. It performs the full pipeline: IQ capture → FM discriminator → DC block → de-emphasis → decimation → baseband slicer → FLEX decoder.

Requires `librtlsdr-dev` (`apt install librtlsdr-dev`).

```bash
# Listen on a FLEX paging frequency
./flex_sdr -f 929.6625

# With manual gain and verbose stats
./flex_sdr -f 929.6625 -g 400 -v

# Inverted polarity (some receivers invert)
./flex_sdr -f 929.6625 -i

# Custom SDR and audio sample rates
./flex_sdr -f 929.6625 -s 240000 -r 48000
```

Options:
| Flag | Default | Description |
|------|---------|-------------|
| `-f freq` | (required) | Frequency in MHz |
| `-d index` | 0 | RTL-SDR device index |
| `-g gain` | auto | Tuner gain in dB*10 (e.g., `-g 400` = 40.0 dB) |
| `-s rate` | 240000 | SDR sample rate in Hz |
| `-r rate` | 48000 | Audio decimation rate in Hz |
| `-i` | off | Invert FM discriminator polarity |
| `-v` | off | Print periodic statistics |

The receiver auto-detects the FLEX speed from the sync marker -- no need to specify baud rate. All four speeds (1600/3200/6400 bps) are decoded automatically.

## Project Structure

```
libflex/
  .github/workflows/
    ci.yml              CI build + test on push/PR
    release.yml         Multi-arch .deb release on version tags
  include/libflex/
    flex.h              Umbrella header (includes all public headers)
    version.h           Version constants (0.1.0)
    error.h             Error codes and flex_strerror()
    types.h             Core types, protocol constants, flex_msg_t, speed helpers
    bch.h               BCH(31,21) encode, syndrome, 2-bit correct, parity
    sync.h              Sync marker (0xA6C6AAAA), mode words, speed detection
    fiw.h               Frame Info Word encode/decode (cycle, frame, repeat)
    biw.h               Block Information Word encode/decode (field boundaries)
    encoder.h           Frame encoder API (single + batch, all speeds)
    decoder.h           Streaming decoder API with callback (6-state machine)
    modem.h             FSK modulator, Goertzel demodulator, baseband, WAV I/O
  src/
    flex_internal.h     Private types, bitstream reader/writer (MSB + LSB), prototypes
    error.c             Error string table
    bch.c               BCH(31,21) codec with precomputed 528-entry syndrome table
    sync.c              Sync1 (mode|marker|~mode), Hamming-distance speed detection
    fiw.c               Frame Info Word codec with nibble checksum
    biw.c               Block Information Word codec
    codeword.c          Short/long address, vector (with page type mapping), data cws
    interleave.c        8x32 block interleave/deinterleave (LSB-first)
    numeric.c           BCD numeric encode/decode (5 digits per 20-bit chunk)
    alpha.c             7-bit alphanumeric pack/unpack (3 chars per 21-bit word)
    binary.c            Binary message encode/decode (configurable bit width)
    phase.c             Multi-phase separation/combination, speed helpers
    encoder.c           Multi-speed frame encoder with preamble, inverted sync, headers
    decoder.c           Streaming 6-state decoder with frame-level reassembly
    modem.c             Goertzel FSK modem + baseband generator + WAV file I/O
  tests/                131 tests across 14 test files
  examples/
    encode_page.c       Encode a page to raw bitstream on stdout
    decode_stream.c     Decode raw bitstream from stdin
    encode_wav.c        Encode a page to WAV audio file (FSK tones)
    decode_wav.c        Decode WAV audio file to messages
    gen_baseband.c      Generate NRZ/4-level baseband WAVs for multimon-ng
    gen_samples.c       Generate sample WAVs for all combinations
    flex_sdr.c          Live RTL-SDR FLEX receiver (requires librtlsdr)
  samples/              48 pre-generated WAV files (all types x speeds x rates)
  debian/               Debian packaging files
  configure.ac          Autoconf configuration
  Makefile.am           Top-level automake
  autogen.sh            Bootstrap script
  libflex.pc.in         pkg-config template
```

## Technical Details

### BCH(31,21) Error Correcting Code

The FLEX BCH code uses generator polynomial g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 (0x769), the same polynomial as POCSAG. Codeword layout: data in bits 0-20 (LSB), BCH parity in bits 21-30, even parity in bit 31. It can:
- Correct any 1-bit error (returns 1)
- Correct any 2-bit error via precomputed syndrome table (returns 2)
- Detect 3+ bit errors (returns -1, uncorrectable)

The syndrome table is `static const` (528 entries computed at compile time), making the BCH module fully thread-safe with no initialization required.

### Block Interleaving

FLEX uses 8x32 bit matrix interleaving within each data block:
1. 8 codewords (32 bits each) arranged as rows
2. Transmitted column-by-column, LSB first (bit 0 of all codewords, then bit 1, etc.)
3. Spreads burst errors across multiple codewords

A burst error of up to 16 consecutive bits in the interleaved stream results in at most 2 errors per codeword after deinterleaving, which is fully correctable by BCH.

### Encoder

The encoder generates a complete FLEX frame matching the protocol specification:
1. **Preamble**: 960 alternating bits for receiver PLL clock recovery
2. **Sync1** (inverted): mode word (A) + sync marker (B) + ~mode word (C), all bit-inverted
3. **Dotting**: 16 alternating bits between Sync1 and FIW
4. **FIW**: BCH-protected Frame Info Word with cycle/frame number, transmitted LSB-first
5. **Sync2**: Alternating dotting bits at the symbol baud rate (25ms)
6. **Data Blocks**: 11 blocks of 8 interleaved codewords per phase
7. **Trailing idle**: 64 alternating bits for receiver decode trigger

Frame data layout (Phase A): BIW + addresses + vectors + message data (header + content words). Other phases carry alternating idle patterns for PLL lock. Each block is interleaved independently.

Alpha messages use a 7-bit skip on the first content word (frag=3 convention). Vector codewords use FLEX spec page type values (ALPHA=5, NUMERIC=3, TONE=2, BINARY=6).

### Decoder

The streaming decoder is a 6-state machine:
1. **HUNTING**: Shifts bits into a 32-bit register, checks for inverted sync marker (`~0xA6C6AAAA`)
2. **SYNC1**: Reads 16-bit mode word (C-word), detects speed (with Hamming tolerance)
3. **DOTTING1**: Skips 16 bits of dotting between Sync1 and FIW
4. **FIW**: Reads 32-bit FIW codeword LSB-first, BCH-corrects, extracts cycle/frame
5. **SYNC2**: Skips Sync2 dotting bits (duration = symbol_baud * 25ms)
6. **BLOCK**: Accumulates 11 blocks, deinterleaves (LSB-first), BCH-corrects per phase

After all 11 blocks: parses BIW for field boundaries, decodes addresses (with page type reverse mapping), skips message headers, applies 7-bit alpha shift, extracts and decodes message content, fires callback for each complete message. Returns to HUNTING.

### FSK Modem

The modulator generates continuous-phase FSK audio:
- **2-FSK**: mark/space tones at center +/- deviation
- **4-FSK**: four tones at center +/- outer deviation and center +/- inner deviation (3:1 ratio)
- Baseband frequencies adapt to the sample rate to keep all tones within Nyquist
- Phase accumulator ensures exact symbol timing with no drift

The demodulator uses the **Goertzel algorithm** for efficient tone detection:
- Computes DFT energy at mark and space frequencies (2-FSK) or all 4 tone frequencies (4-FSK)
- One Goertzel iteration per sample (2 multiplies + 1 add per tone)
- Power comparison at symbol boundaries determines bit/symbol values
- No FFT needed -- Goertzel is O(N) per tone vs O(N log N) for full FFT

### Baseband Generator

`flex_baseband()` produces NRZ/4-level samples suitable for feeding an FM transmitter or for direct decoding by multimon-ng:
- Uses 16-bit fixed-point phase accumulation matching multimon-ng's `gen_flex.c` timing
- Handles baud rate transition from 1600 baud (header) to mode baud (data) mid-frame
- 2-FSK: +0.73/-0.73 levels; 4-FSK: Gray-coded +/-0.73 and +/-0.24 levels
- Compatible with `multimon-ng -t wav -a FLEX`

## License

MIT -- see [LICENSE](LICENSE).
