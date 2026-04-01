# FLEX Frame-Synchronized Transmission

## Problem

FLEX pagers use battery-saving mode: they wake up only during their assigned frame slot (`capcode % 128`), listen for their address, then sleep. If the transmitter sends at the wrong time, the pager never sees the message.

The current libflex API encodes and modulates immediately — the caller is responsible for transmission timing. This works for SDR loopback testing but not for real pagers.

## FLEX Time Structure

```
1 hour  = 15 cycles × 4 minutes each
1 cycle = 128 frames × 1.875 seconds each
Frame 0 of cycle 0 starts at the top of the hour (UTC)
```

Each pager listens on frame `capcode % 128`. The pager wakes ~100ms before its frame boundary, listens through the frame (1.875s), then sleeps until the next cycle (4 minutes later).

## Frame Timing Math

```c
/* Given UTC time, compute the active cycle and frame */
void flex_frame_at_time(time_t utc, uint16_t *cycle, uint16_t *frame)
{
    struct tm t;
    gmtime_r(&utc, &t);
    int sec_in_hour = t.tm_min * 60 + t.tm_sec;
    *cycle = (sec_in_hour / 240) % 15;          /* 240s per cycle */
    int sec_in_cycle = sec_in_hour % 240;
    *frame = (uint16_t)((sec_in_cycle * 1000) / 1875) % 128;
}

/* Given a capcode, compute when the next TX window opens */
void flex_next_frame_time(uint32_t capcode, struct timespec *now,
                          struct timespec *when)
{
    uint16_t target_frame = capcode % 128;
    uint16_t cur_cycle, cur_frame;
    flex_frame_at_time(now->tv_sec, &cur_cycle, &cur_frame);

    /* frames until target */
    int delta = (int)target_frame - (int)cur_frame;
    if (delta <= 0) delta += 128;   /* next cycle */

    /* each frame = 1.875s = 1875ms */
    int64_t wait_ms = delta * 1875;

    when->tv_sec  = now->tv_sec + (wait_ms / 1000);
    when->tv_nsec = now->tv_nsec + (wait_ms % 1000) * 1000000;
    if (when->tv_nsec >= 1000000000) {
        when->tv_sec++;
        when->tv_nsec -= 1000000000;
    }
}
```

## Proposed API

### Types

```c
/* Callback: "transmit this audio right now" */
typedef void (*flex_tx_cb_t)(const int16_t *pcm, size_t nsamples, void *user);

/* Opaque scheduler handle (only part of libflex that allocates) */
typedef struct flex_sched flex_sched_t;

/* Pending page info (returned by status queries) */
typedef struct {
    uint32_t        capcode;
    flex_msg_type_t type;
    flex_speed_t    speed;
    uint16_t        target_frame;
    struct timespec fire_time;
} flex_sched_entry_t;
```

### Lifecycle

```c
/* Create scheduler.
 * sample_rate: audio sample rate for modulation
 * modulation:  0 = baseband, 1 = FSK
 * callback:    called at the frame boundary with ready-to-play audio
 * user:        passed to callback
 */
flex_sched_t *flex_sched_create(float sample_rate, int modulation,
                                flex_tx_cb_t callback, void *user);

void flex_sched_destroy(flex_sched_t *sched);
```

### Queueing

```c
/* Queue a page for time-synchronized transmission.
 * The scheduler computes the target frame from the capcode,
 * pre-encodes and pre-modulates the audio, and holds it
 * until the frame boundary arrives.
 *
 * Returns FLEX_OK on success. The page fires on the next
 * occurrence of frame (capcode % 128).
 */
flex_err_t flex_sched_page(flex_sched_t *sched,
                           uint32_t capcode,
                           flex_msg_type_t type,
                           flex_speed_t speed,
                           const char *text);

/* Queue a numeric page */
flex_err_t flex_sched_numeric(flex_sched_t *sched,
                              uint32_t capcode,
                              const char *digits);

/* Queue a tone-only page */
flex_err_t flex_sched_tone(flex_sched_t *sched,
                           uint32_t capcode);

/* Cancel all pending pages for a capcode (0 = cancel all) */
flex_err_t flex_sched_cancel(flex_sched_t *sched, uint32_t capcode);
```

### Tick

```c
/* Check the clock and fire any pages whose frame boundary has arrived.
 * Must be called periodically — at least every 50ms for reliable timing.
 * Safe to call from a single thread (no internal locking).
 *
 * Returns the number of pages fired (0 if nothing was due).
 */
int flex_sched_tick(flex_sched_t *sched);
```

### Status

```c
/* Number of pages currently queued */
int flex_sched_pending(const flex_sched_t *sched);

/* Next fire time (NULL if queue empty) */
const struct timespec *flex_sched_next_fire(const flex_sched_t *sched);
```

### Frame Utilities (stateless, no allocation)

```c
/* Compute cycle/frame for a given UTC time */
void flex_frame_at_time(time_t utc, uint16_t *cycle, uint16_t *frame);

/* Compute the next TX time for a capcode after a given time */
void flex_next_frame_time(uint32_t capcode,
                          const struct timespec *after,
                          struct timespec *when);

/* Which frame does a capcode belong to? */
static inline uint16_t flex_capcode_frame(uint32_t capcode) {
    return capcode % 128;
}
```

## Kerchunk Integration

### mod_flex.c changes

```c
static flex_sched_t *g_sched;

/* TX callback — fired by libflex at the frame boundary */
static void on_tx_ready(const int16_t *pcm, size_t ns, void *user)
{
    kerchunk_core_t *core = (kerchunk_core_t *)user;
    /* Audio is pre-encoded and pre-modulated.
     * Just hand it to the audio queue. PTT is handled
     * automatically by kerchunk's queue_audio_buffer. */
    core->queue_audio_buffer(pcm, ns, KERCHUNK_PRI_NORMAL);
}

/* Module init */
static int mod_configure(...)
{
    g_sched = flex_sched_create(
        (float)g_core->sample_rate,
        g_use_fsk,
        on_tx_ready, g_core);
}

/* CLI handler — no change to command syntax */
static int cli_flex_send(...)
{
    /* Old: flex_tx(capcode, type, speed, text) — immediate TX
     * New: flex_sched_page(g_sched, ...) — queued for frame boundary */
    flex_sched_page(g_sched, capcode, FLEX_MSG_ALPHA, speed, msg);
    resp_str(resp, "status", "scheduled");
    resp_int(resp, "target_frame", capcode % 128);
    /* ... */
}

/* Kerchunk main loop already ticks modules periodically.
 * Add a tick call (or hook into kerchunk's timer system): */
static int mod_tick(void)
{
    if (g_sched)
        flex_sched_tick(g_sched);
    return 0;
}
```

### CLI output change

```
kerchunk> flex send 1234567 1600 "Hello pager"
{
  "status": "scheduled",
  "capcode": 1234567,
  "target_frame": 103,
  "fire_in_ms": 2340,
  "speed": 1600
}
```

## Internal Design

### Pre-encoding

When `flex_sched_page()` is called, the scheduler immediately:
1. Computes `target_frame = capcode % 128`
2. Encodes the FLEX frame with `flex_encode_single()`, stamping the FIW with the correct cycle/frame
3. Modulates to PCM audio (FSK or baseband)
4. Stores the ready-to-play PCM buffer and the fire time

This means `flex_sched_tick()` does zero DSP work — it just checks the clock and fires the callback.

### Queue

Simple linked list or ring buffer of `flex_sched_entry_t` structs, each holding:
- Target frame number
- Pre-computed fire time (`struct timespec`)
- PCM audio buffer (malloc'd)
- Entry metadata (capcode, type, speed for status reporting)

Sorted by fire time. `tick()` checks the head — if `clock_gettime >= fire_time`, pop and fire.

### FIW Cycle/Frame

The FIW must contain the **actual** cycle and frame numbers for the time the signal is transmitted. Since we pre-encode at queue time but transmit later, the FIW cycle/frame is set based on the **target fire time**, not the current time.

```c
/* At queue time: */
uint16_t cycle, frame;
flex_frame_at_time(fire_time.tv_sec, &cycle, &frame);
flex_encoder_set_frame(&enc, cycle, frame);
```

### Timing Budget

```
Frame window:      1875 ms
Pager wake-up:     ~100 ms before frame boundary
TX delay (PTT):    ~100 ms
Preamble:          960 bits @ 1600 baud = 600 ms
Sync + data:       ~1800 ms @ 1600 baud

Total TX time:     ~2500 ms (extends past frame boundary — OK per spec)
Start TX:          ~200 ms before frame boundary (preamble arrives as pager wakes)
```

The scheduler should fire the callback ~200ms before the frame boundary so the preamble is in the air when the pager wakes up. This accounts for:
- PTT key-up delay (~100ms)
- Audio queue latency (~50ms)
- Preamble transmission time before sync

### Clock Requirements

- **NTP**: typical accuracy <10ms on LAN — sufficient
- **GPS PPS**: <1µs — ideal but not required
- **Free-running RTC**: drifts ~1s/day — not sufficient

The scheduler should log a warning if it detects the system clock is not NTP-synchronized (`adjtimex()` or checking `/run/systemd/timesync/synchronized`).

## What This Does NOT Do

- **Multi-frame batching**: Each `flex_sched_page()` creates one frame. Multiple pages to different capcodes on the same frame could be batched — future optimization.
- **Repeat transmission**: Commercial FLEX systems retransmit on subsequent cycles. The caller can re-queue if needed.
- **Roaming/simulcast**: Single transmitter only.
- **Collapse window**: No de-duplication of rapid re-pages to the same capcode.

## Files to Create/Modify

| File | Action |
|------|--------|
| `include/libflex/sched.h` | New — scheduler API |
| `include/libflex/flex.h` | Add `#include "sched.h"` |
| `src/sched.c` | New — scheduler + frame timing implementation |
| `src/Makefile.am` | Add sched.c |
| `include/Makefile.am` | Add sched.h |
| `tests/test_sched.c` | New — frame timing math tests, mock TX tests |
| `tests/test_main.c` | Register new test suite |
| `tests/Makefile.am` | Add test_sched.c |
