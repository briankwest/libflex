# libflex

C library for **FLEX** (Motorola) paging protocol encoding, decoding, and FSK modulation/demodulation. Generates and parses complete FLEX frame bitstreams with sync detection, BCH error correction (2-bit), block interleaving, multi-phase support, multi-speed operation (1600/3200/6400 bps), and Goertzel-based 2-FSK/4-FSK modem with WAV file I/O.

C99, no external dependencies beyond libc and libm. All structs are stack-allocatable with zero dynamic memory allocation. Thread-safe (no mutable global state).

## Table of Contents

- [Features](#features)
- [Building](#building)
- [API](#api)
  - [Single Message Encoding](#single-message-encoding)
  - [Batch Encoding](#batch-encoding)
  - [Streaming Decoder](#streaming-decoder)
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

- **FLEX Encoder** -- Single or batch message encoding to complete FLEX frame bitstreams with sync fields, Frame Info Word, block interleaving, and multi-phase support at all four speeds
- **FLEX Decoder** -- Streaming 5-state state-machine decoder with sync detection, speed auto-detection, callback-driven message delivery, and bit/byte/symbol input
- **FSK Modem** -- Goertzel-based 2-FSK and 4-FSK modulator/demodulator with adaptive baseband frequencies, WAV file read/write, and support for 8/16/32/48 kHz sample rates
- **BCH(31,21) Codec** -- Full encode, syndrome check, 1-bit and 2-bit error correction via precomputed compile-time syndrome table (528 entries, fully thread-safe)
- **Block Interleaving** -- 32x8 matrix interleave/deinterleave for burst error protection; 16-bit burst errors fully correctable after deinterleaving
- **Multi-Speed** -- 1600 bps (2-FSK), 3200 bps (2-FSK or 4-FSK), 6400 bps (4-FSK) with automatic multi-phase encoding/decoding
- **Numeric Messages** -- BCD encoding/decoding with digits 0-9 and special characters `*U -[]()`
- **Alphanumeric Messages** -- 7-bit ASCII packing (3 characters per 21-bit codeword data field)
- **Binary Messages** -- Arbitrary binary data with configurable bit width (1-16 bits per item)
- **Tone-Only Pages** -- Address-only pages with no message data
- **Zero Allocation** -- All encoder, decoder, modulator, and demodulator contexts are fixed-size structs, stack-allocatable with no malloc
- **Thread-Safe** -- No mutable global state; BCH syndrome table is `static const`

### Supported Parameters

| Parameter | Values |
|-----------|--------|
| Speeds | 1600/2, 3200/2, 3200/4, 6400/4 bps |
| Phases | 1 (1600), 2 (3200), 4 (6400) |
| Message types | Numeric, alphanumeric, binary, tone-only |
| Addresses | Short (single codeword) and long (two codewords) |
| BCH correction | Up to 2-bit correction, 3+ bit detection |
| Interleaving | 32x8 block interleave per block per phase |
| Messages per frame | Up to 32 queued |
| Modem sample rates | 8000, 16000, 32000, 48000 Hz |
| Modulation | 2-FSK (1600/3200 bps), 4-FSK (3200/6400 bps) |

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

/* Messages are packed into the address/vector/data fields of the frame.
 * Multiple messages share the same frame efficiently. */
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
- Sync marker detection (`0xA6C6AAAA`) from arbitrary bit positions
- Speed auto-detection from 16-bit mode word (with Hamming distance tolerance)
- FIW decode for cycle/frame metadata
- BCH 2-bit error correction on every codeword
- Block deinterleaving at all speeds
- Multi-phase data separation (1/2/4 phases)
- Address/vector/message field reassembly after full frame decode

### FSK Modem

Modulate FLEX bitstreams to audio and demodulate back:

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

/* Build a valid 32-bit codeword from 21 data bits */
uint32_t cw = flex_codeword_build(data21);

/* Check a codeword (returns 0 if valid) */
uint32_t syndrome = flex_bch_syndrome(cw);

/* Attempt up to 2-bit error correction (returns 0 on success) */
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
| Modulation | 2-FSK (1600, 3200) and 4-FSK (3200, 6400) |
| Sync marker | `0xA6C6AAAA` (always at 1600 bps) |
| Mode words | `0x870C` (1600/2), `0x7B18` (3200/2), `0xB068` (3200/4), `0xDEA0` (6400/4) |
| Timing | 15 cycles/hour, 128 frames/cycle, 1.875s per frame |
| Frame structure | Sync1 + FIW + Sync2 + 11 data blocks |
| Block size | 8 interleaved codewords per phase |
| Codeword | 32 bits: 21 data + 10 BCH + 1 parity |
| Error correction | BCH(31,21) with generator polynomial 0x769, corrects up to 2 bits |
| Interleaving | 32x8 matrix, column-wise transmission |

### Bitstream Structure

```
[  Sync1: 16-bit bit sync + 32-bit marker + 16-bit mode word  ]  (64 bits, 1600 bps)
[  FIW: 32-bit BCH-protected Frame Info Word                   ]  (32 bits, 1600 bps)
[  Sync2: 32-bit sync marker                                   ]  (32 bits, data rate)
[  Block 0: 8 interleaved codewords x nphases                  ]  (256-1024 bits)
[  Block 1: ...                                                 ]
[  ...                                                          ]
[  Block 10: ...                                                ]
```

### Frame Size by Speed

| Speed | Phases | Bits/Block | Total Frame Bits |
|-------|--------|------------|-----------------|
| 1600/2 | 1 | 256 | 2,944 |
| 3200/2 | 2 | 512 | 5,760 |
| 3200/4 | 2 | 512 | 5,760 |
| 6400/4 | 4 | 1,024 | 11,392 |

### Data Block Layout (Phase A)

```
Word 0:       BIW (Block Information Word)
Words 1..Na:  Address codewords (short: 1 cw, long: 2 cw)
Words Na..Nv: Vector codewords (1:1 with addresses, encode msg type/location/length)
Words Nv..87: Message data codewords
Unused:       Idle (all zeros, valid BCH)
```

### Codeword Format

```
Address codeword (short):
  bits 31-11 = capcode + 32768 (21 data bits)
  bits 10-1  = BCH(31,21) parity (10 bits)
  bit  0     = even parity

Vector codeword:
  bits 20-14 = message word count (7 bits)
  bits 13-7  = message start word index (7 bits)
  bits 6-4   = message type (3 bits)
  bits 3-0   = checksum (nibble sum = 0xF)
  + BCH parity + even parity

Data codeword:
  bits 31-11 = 21 data bits
  bits 10-1  = BCH(31,21) parity
  bit  0     = even parity
```

## Test Suite

```
$ make check

libflex test suite

BCH:
  test_bch_build_verify                                   PASS
  test_bch_build_verify_zero                              PASS
  test_bch_build_verify_max                               PASS
  test_bch_encode_zero                                    PASS
  test_bch_correct_single                                 PASS
  test_bch_correct_parity_bit                             PASS
  test_bch_correct_double                                 PASS
  test_bch_correct_double_adjacent                        PASS
  test_bch_correct_double_extremes                        PASS
  test_bch_detect_triple                                  PASS
  test_bch_all_single_bits_correctable                    PASS
  test_bch_all_double_bits_correctable                    PASS

Interleave:
  test_interleave_roundtrip                               PASS
  test_interleave_known_pattern                           PASS
  test_interleave_zeros                                   PASS
  test_interleave_column_order                            PASS
  test_deinterleave_bch_valid                             PASS
  test_interleave_burst_correction                        PASS

Sync:
  test_sync_detect_1600_2                                 PASS
  test_sync_detect_3200_2                                 PASS
  test_sync_detect_3200_4                                 PASS
  test_sync_detect_6400_4                                 PASS
  test_sync_detect_fuzzy                                  PASS
  test_sync_detect_fuzzy_3bit                             PASS
  test_sync_detect_invalid                                PASS
  test_sync_hamming                                       PASS
  test_sync1_build                                        PASS
  test_sync1_build_all_speeds                             PASS
  test_sync2_build                                        PASS

FIW:               7 tests   BIW: 6 tests   Numeric: 6 tests
Alpha: 5 tests   Binary: 6 tests   Codeword: 10 tests   Phase: 7 tests

Encoder:
  14 tests -- all types, all speeds, sync/FIW/BIW/BCH validation

Decoder:
  12 tests -- all types at 1600/3200/6400, multi-message, stats, reset

Roundtrip:
  10 tests -- full encode-decode at all speeds, multi-message, edge cases

Modem:
  21 tests -- modulator, WAV I/O, Goertzel demod roundtrip at all
  4 FLEX speeds x 4 sample rates (48k/32k/16k/8k)

131 passed, 0 failed
```

Tests cover:
- BCH(31,21) encode/syndrome/correct for all 32 bit positions, 2-bit correction, 3-bit detection
- 32x8 block interleave/deinterleave round-trips with burst error correction verification
- Sync marker detection at all 4 speeds with Hamming-distance fuzzy matching
- FIW and BIW encode/decode round-trips with BCH error correction
- Numeric BCD, 7-bit alpha, and binary encoding/decoding round-trips
- Short/long address and vector codeword construction with field verification
- Multi-phase separation and combination
- Encoder output validation: sync position, FIW cycle/frame, BIW field boundaries, BCH validity on all 88 codewords
- Streaming decoder at all 4 speeds with multi-message, statistics, and reset
- Full encode-then-decode round-trips for numeric, alpha, binary, and tone at all speeds
- FSK modulator/demodulator round-trip through audio at 48/32/16/8 kHz sample rates
- WAV file write/read round-trip with 16-bit PCM quantization verification

## Example Programs

| Program | Description |
|---------|-------------|
| `encode_page` | Encode a pager message, write raw FLEX frame bitstream to stdout |
| `decode_stream` | Read raw FLEX bitstream from stdin, print decoded messages |
| `encode_wav` | Encode a pager message to a WAV audio file |
| `decode_wav` | Decode a FLEX WAV audio file back to messages |
| `gen_samples` | Generate sample WAV files for all type/speed/rate combinations |

```bash
# Encode a numeric page to raw bitstream
./encode_page 1234 n "5551234" > page.bin

# Encode an alpha page at 6400 bps
./encode_page 2000 a 6400 "Hello FLEX" > page.bin

# Encode a tone-only page
./encode_page 500 t > page.bin

# Decode a raw bitstream
./decode_stream < page.bin
# [NUM] capcode=1234 cycle=0 frame=0 speed=1600 msg="5551234"

# Pipe encode directly to decode
./encode_page 1234 n "5551234" | ./decode_stream

# Encode to WAV audio file
./encode_wav 1234 n "5551234" -o page.wav
# wrote page.wav: 88320 samples (1.84 sec) at 48000 Hz, 1600 bps

# Decode from WAV audio file
./decode_wav page.wav
# [NUM] capcode=1234 cycle=0 frame=0 speed=1600 msg="5551234"

# Encode at 6400 bps to WAV
./encode_wav 2000 a 6400 "Fast FLEX!" -o fast.wav

# Decode 6400 bps WAV
./decode_wav fast.wav 6400
# [ALPHA] capcode=2000 cycle=0 frame=0 speed=6400 msg="Fast FLEX!"

# Generate all sample WAV files (48 files across all speeds/rates/types)
./gen_samples samples/
```

## Project Structure

```
libflex/
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
    decoder.h           Streaming decoder API with callback (5-state machine)
    modem.h             FSK modulator, Goertzel demodulator, WAV I/O
  src/
    flex_internal.h     Private types, bitstream reader/writer, internal prototypes
    error.c             Error string table
    bch.c               BCH(31,21) codec with precomputed 528-entry syndrome table
    sync.c              Sync1/Sync2 pattern tables, Hamming-distance speed detection
    fiw.c               Frame Info Word codec with nibble checksum
    biw.c               Block Information Word codec with nibble checksum
    codeword.c          Short/long address, vector, data codeword construction
    interleave.c        32x8 block interleave/deinterleave
    numeric.c           BCD numeric encode/decode (5 digits per 20-bit chunk)
    alpha.c             7-bit alphanumeric pack/unpack (3 chars per 21-bit word)
    binary.c            Binary message encode/decode (configurable bit width)
    phase.c             Multi-phase separation/combination, speed helpers
    encoder.c           Multi-speed frame encoder with address/vector/data packing
    decoder.c           Streaming 5-state decoder with frame-level reassembly
    modem.c             Goertzel FSK modem + WAV file I/O
  tests/
    test.h              Test harness macros (ASSERT, RUN_TEST, etc.)
    test_main.c         Test runner (131 tests across 14 test files)
    test_bch.c          BCH encode/syndrome/correct tests (12 tests)
    test_interleave.c   Block interleave round-trip + burst correction (6 tests)
    test_sync.c         Sync detection + speed identification (11 tests)
    test_fiw.c          FIW encode/decode round-trips (7 tests)
    test_biw.c          BIW encode/decode round-trips (6 tests)
    test_numeric.c      BCD numeric round-trips (6 tests)
    test_alpha.c        Alpha encoding round-trips (5 tests)
    test_binary.c       Binary encoding round-trips (6 tests)
    test_codeword.c     Codeword construction tests (10 tests)
    test_phase.c        Phase separation tests (7 tests)
    test_encoder.c      Encoder output validation (14 tests)
    test_decoder.c      Streaming decoder tests (12 tests)
    test_roundtrip.c    Full encode-decode round-trips (10 tests)
    test_modem.c        FSK modem round-trips at all speeds/rates (19 tests)
  examples/
    encode_page.c       Encode a page to raw bitstream on stdout
    decode_stream.c     Decode raw bitstream from stdin
    encode_wav.c        Encode a page to WAV audio file
    decode_wav.c        Decode WAV audio file to messages
    gen_samples.c       Generate sample WAVs for all combinations
  samples/              48 pre-generated WAV files (all types x speeds x rates)
  debian/               Debian packaging files
  configure.ac          Autoconf configuration
  Makefile.am           Top-level automake
  autogen.sh            Bootstrap script
  libflex.pc.in         pkg-config template
```

## Technical Details

### BCH(31,21) Error Correcting Code

The FLEX BCH code uses generator polynomial g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 (0x769), the same polynomial as POCSAG. It protects the 31-bit field (bits 31-1) and can:
- Correct any 1-bit error via syndrome lookup
- Correct any 2-bit error via precomputed syndrome table (528 entries mapping all C(32,2) error patterns)
- Detect 3+ bit errors (returns uncorrectable)

The syndrome table is `static const` (computed at compile time), making the BCH module fully thread-safe with no initialization required.

### Block Interleaving

FLEX uses 32x8 bit matrix interleaving within each data block:
1. 8 codewords (32 bits each) arranged as rows
2. Transmitted column-by-column (bit 31 of all codewords first, then bit 30, etc.)
3. Spreads burst errors across multiple codewords

A burst error of up to 16 consecutive bits in the interleaved stream results in at most 2 errors per codeword after deinterleaving, which is fully correctable by BCH.

### Encoder

The encoder generates a complete FLEX frame:
1. **Sync1**: 16-bit alternating bit sync + 32-bit sync marker + 16-bit mode word (64 bits at 1600 bps)
2. **FIW**: BCH-protected Frame Info Word with cycle/frame number (32 bits)
3. **Sync2**: Sync marker at data rate (32 bits)
4. **Data Blocks**: 11 blocks of 8 interleaved codewords per phase

Frame data layout (Phase A): BIW + addresses + vectors + message data + idle fill. Other phases are idle. Each block is interleaved independently.

### Decoder

The streaming decoder is a 5-state machine:
1. **HUNTING**: Shifts bits into a 32-bit register, checks for sync marker
2. **SYNC1**: Reads 16-bit mode word, detects speed (with Hamming tolerance)
3. **FIW**: Reads and BCH-corrects the Frame Info Word
4. **SYNC2**: Reads and validates sync marker at data rate
5. **BLOCK**: Accumulates 11 blocks, deinterleaves, BCH-corrects per phase

After all 11 blocks: parses BIW for field boundaries, decodes addresses and vectors, extracts and decodes message data, fires callback for each complete message. Returns to HUNTING.

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

## License

MIT -- see [LICENSE](LICENSE).
