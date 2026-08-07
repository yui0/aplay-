// aplay+engine.h
// ©2017-2026 Yuichiro Nakada
// MIT license
//
// Single-header playback engine shared by aplay+.c (TUI) and aplay+ui.c (TUI+GUI).
//
// Usage (TUI build):
//   #define APLAY_ENGINE_IMPLEMENTATION
//   #include "aplay+engine.h"
//   // then define: static void wait_for_keypress(void); int key(AUDIO*, tui_state_t*);
//
// Usage (GUI build — two-pass include to allow the tui_render macro):
//   #include "aplay+engine.h"          // step 1: declarations only (no define yet)
//   // ... define tui_render wrapper macro here ...
//   #define APLAY_ENGINE_IMPLEMENTATION
//   #include "aplay+engine.h"          // step 2: expand implementations
//   // then define: wait_for_keypress, key
//
// The caller must define in the same translation unit (after including this header):
//   int key(AUDIO *a, tui_state_t *ts);   -- keyboard / injected-key handler

// ============================================================
// Part 1: Declarations  (compiled once per TU via include guard)
// ============================================================
#ifndef APLAY_ENGINE_H
#define APLAY_ENGINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>

/*
 * AUDIO_release() is used for the GUI/TUI hard-stop path.  The original
 * helper drains ALSA before closing, which makes Stop wait for all queued
 * audio.  Keep the original implementation under a private name and expose
 * a low-latency release that drops queued frames immediately.
 */
#define AUDIO_release aplay_audio_release_drain
#include "alsa.h"
#undef AUDIO_release
static inline void AUDIO_release(AUDIO *thiz)
{
	if (!thiz || !thiz->handle) return;
	(void)snd_pcm_drop(thiz->handle);
	snd_pcm_close(thiz->handle);
	thiz->handle = NULL;
}

#include "regexp.h"
#include "random.h"
#include "ls.h"
#include "kbhit.h"
#include "tui.h"

// ---- Playback flags (bitmask passed through play_dir -> play_*) ----
#define USE_FLOAT32    128
#define USE_CROSSTALK  256
#define USE_TEST_MODE  512
#define USE_DSD_ENCODE 1024
#define USE_SUPER_RES  2048

// ---- Audio render constants ----
/*
 * Decoder/DSP blocks stay at 512 frames for throughput, while the ALSA period
 * is smaller.  This separates decode efficiency from device/control latency:
 * 256 output frames are about 5.8 ms at 44.1 kHz.
 */
#define FRAMES                 512  /* decoder/DSP block: keep throughput */
#define AUDIO_PERIOD_FRAMES    256  /* ALSA period: ~5.8 ms @ 44.1 kHz */
#define DSD_ENC_INPUT_CHUNK     32
#define AUDIO_SCRATCH_FRAMES  8192
#define AUDIO_WAIT_MS           20  /* bound control stalls on unusual devices */
#define UI_REFRESH_INTERVAL_NS 50000000ULL  /* 20 Hz */
#define MAX_DELAY_SAMPLES       16
#define SEEK_SECONDS            10
#define VOLUME_STEP             0.05f
#define XTC_ATTEN_STEP          0.05f
#define KEY_POLL_INTERVAL_US  4000  // gate kbhit() to ≤250 calls/sec

#define APLAY_EQ_BANDS           10  /* ISO-style octave-band equalizer */

// ---- Synthetic key codes (above ASCII range, never clash with real bytes) ----
#define KEY_UP    1001
#define KEY_DOWN  1002
#define KEY_LEFT  1003
#define KEY_RIGHT 1004

// ---- Format-filter cycle (used in key(); statics so each TU has its own copy) ----
static const char *fmt_cycle[] = { NULL, "flac", "mp3", "m4a", "ogg", "wav", "wma", "dsf", "dff" };
static const int   fmt_cycle_n = sizeof(fmt_cycle) / sizeof(fmt_cycle[0]);
static int         fmt_filter_idx = 0;

// ---- Global variable declarations ----
extern int    verbose;
extern float  volume;
extern float  xtc_attenuation;
extern int    loop_mode;
extern float  speaker_distance_m;
extern int    cmd;
extern int    track_index, track_total;
extern int    g_flag_diff;
extern char  *dsd_file_path;
extern FILE  *g_dsd_raw_file;
extern char  *fmt_filter;
extern char  *dev;
extern char   g_dev_storage[128];
/* >=0: key() should switch to this device index on the next poll. */
extern volatile int g_device_select_idx;

#define APLAY_MAX_DEVICES 64

/* Low-overhead 10-band equalizer. Gains are dB; setters clamp to ±12 dB.
 * Disabled is the default. Disabled or completely flat settings are a true
 * audio-path bypass: no sample copy, conversion, allocation, or biquad work.
 * The normal PCM route follows the same rule: decoded S16/F32 samples are sent
 * directly to ALSA, and conversion/DSP occurs only when explicitly required. */
void  aplay_equalizer_set_enabled(int enabled);
int   aplay_equalizer_enabled(void);
void  aplay_equalizer_set_preamp(float db);
float aplay_equalizer_preamp(void);
void  aplay_equalizer_set_band(int band, float db);
float aplay_equalizer_band(int band);
float aplay_equalizer_frequency(int band);
void  aplay_equalizer_reset(void);

// ---- key(): implemented per .c file (TUI-only or GUI-aware) ----
int key(AUDIO *a, tui_state_t *ts);

// ---- Engine API ----
void apply_alsa_volume(void);
void display_progress(uint64_t current, uint64_t total, int is_time, const char *label);
void play_wav(char *name, int format, int flag);
void play_flac(char *name, int format, int flag);
void play_dsf(char *name, int format, int flag);
int  play_mp3(char *name, int format, int flag);
void play_ogg(char *name, int flag);
int  play_wma(char *name, int flag);
int  play_aac(char *name, int flag);
void play_test_mode(int format, int flag);
void play_dir(char *name, char *type, char *regexp, int flag);
void set_realtime_priority(void);
void set_cpu(char *c);
void list_alsa_devices(void);
void aplay_copy_dev(const char *name);
/* When -d was omitted: pick the first openable playback hw:N,M (else "default"). */
int  aplay_auto_select_device(void);
void aplay_refresh_devices(void);
int  aplay_device_count(void);
const char *aplay_device_name(int idx);
const char *aplay_device_label(int idx);
int  aplay_device_card(int idx);
int  aplay_device_pcm(int idx);
const char *aplay_device_card_name(int idx);
/* Unique card indices among discovered devices. Returns count written (≤ max_out). */
int  aplay_device_unique_cards(int *out_cards, int max_out);
int  aplay_set_device(AUDIO *a, tui_state_t *ts, const char *name);
int  aplay_cycle_device(AUDIO *a, tui_state_t *ts);
int  aplay_select_device_index(AUDIO *a, tui_state_t *ts, int idx);
/* Call at the top of key() to apply a pending GUI device selection or 'D'. */
int  aplay_handle_device_keys(AUDIO *a, tui_state_t *ts, int c);

#endif /* APLAY_ENGINE_H */


// ============================================================
// Part 2: Implementations  (one TU only; triggered by APLAY_ENGINE_IMPLEMENTATION)
// ============================================================
#if defined(APLAY_ENGINE_IMPLEMENTATION) && !defined(APLAY_ENGINE_H_IMPL)
#define APLAY_ENGINE_H_IMPL

// ---- Codec single-header implementations ----
#ifdef USE_FOXEN_FLAC
#  define FLAC_IMPLEMENTATION
#  include "flac.h"
#else
#  define DR_FLAC_IMPLEMENTATION
#  include "dr_flac.h"
#endif
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#ifdef DR_MP3_IMPLEMENTATION
#  include "dr_mp3.h"
#else
#  include "minimp3.h"
#endif
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR
//#define AAC_ENABLE_SBR
#include "uaac.h"
#include "uwma.h"
#include "stb_vorbis.h"
#define DSD_DECODER_IMPLEMENTATION
#include "dsd.h"
#define DSD_ENCODER_IMPLEMENTATION
#include "dsd_encoder.h"

#define PARG_IMPLEMENTATION
#include "parg.h"

// ---- Global variable definitions ----
int    verbose = 0;
float  volume = 1.0f;
float  xtc_attenuation = 0.4f;
int    loop_mode = 0;
float  speaker_distance_m = 0.5;
int    cmd;
int    track_index = 0, track_total = 0;

// Accumulates the net effect of runtime toggles made with the 'c' (crosstalk)
// 'e' (DSD/DoP), and 's' (super resolution) keys during playback. Each
// play_*() function only ever
// sees a *copy* of the `flag` bitmask passed in by play_dir(), so toggling a
// bit locally does not, by itself, survive into the next track. Since both
// toggles are implemented as `flag ^= BIT`, XORing the same bit into this
// global on every toggle reproduces the exact cumulative effect regardless of
// how many times it was pressed; play_dir() then XORs it back into the
// original command-line flag before starting each new track, so the c/e
// state carries over correctly from one track to the next.
int g_flag_diff = 0;

// -o <path>: when set, every DSD-encoded chunk (whichever track/route produced
// it) is also appended here as a raw bitstream, in addition to (or instead of,
// if playback fails to open) DoP playback. Optional feature, off by default.
char *dsd_file_path = NULL;
FILE *g_dsd_raw_file = NULL;

// Interactive format filter, cycled with the 'f' key during playback (see
// key() below). NULL means "ALL" (no filtering). play_dir() re-checks this
// on every file in its listing, so a change takes effect from the very next
// track without needing to restart the player.
char *fmt_filter = NULL;

char g_dev_storage[128] = "hw:0,0";  // Placeholder; -d or aplay_auto_select_device() sets real target
char *dev = g_dev_storage;
volatile int g_device_select_idx = -1;

/*
 * UI-facing EQ controls use integer tenths of a decibel. Integer atomics avoid
 * locks and avoid racing floating-point objects between the GUI and playback
 * threads. The audio thread only rebuilds coefficients when the revision
 * changes; the normal block path is two relaxed/acquire integer loads.
 */
static int g_eq_enabled = 0;
static int g_eq_preamp_tenths = 0;
static int g_eq_band_tenths[APLAY_EQ_BANDS] = {0};
static unsigned int g_eq_revision = 1;
static const float g_eq_frequencies[APLAY_EQ_BANDS] = {
	31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
	1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

static int aplay_eq_db_to_tenths(float db)
{
	int v;
	if (db > 12.0f) db = 12.0f;
	if (db < -12.0f) db = -12.0f;
	v = (int)lroundf(db * 10.0f);
	if (v > 120) v = 120;
	if (v < -120) v = -120;
	return v;
}

static void aplay_eq_touch(void)
{
	(void)__atomic_add_fetch(&g_eq_revision, 1u, __ATOMIC_RELEASE);
}

void aplay_equalizer_set_enabled(int enabled)
{
	int v = enabled ? 1 : 0;
	if (__atomic_exchange_n(&g_eq_enabled, v, __ATOMIC_RELAXED) != v)
		aplay_eq_touch();
}

int aplay_equalizer_enabled(void)
{
	return __atomic_load_n(&g_eq_enabled, __ATOMIC_RELAXED);
}

void aplay_equalizer_set_preamp(float db)
{
	int v = aplay_eq_db_to_tenths(db);
	if (__atomic_exchange_n(&g_eq_preamp_tenths, v,
	                        __ATOMIC_RELAXED) != v)
		aplay_eq_touch();
}

float aplay_equalizer_preamp(void)
{
	return __atomic_load_n(&g_eq_preamp_tenths,
	                       __ATOMIC_RELAXED) * 0.1f;
}

void aplay_equalizer_set_band(int band, float db)
{
	int v;
	if (band < 0 || band >= APLAY_EQ_BANDS) return;
	v = aplay_eq_db_to_tenths(db);
	if (__atomic_exchange_n(&g_eq_band_tenths[band], v,
	                        __ATOMIC_RELAXED) != v)
		aplay_eq_touch();
}

float aplay_equalizer_band(int band)
{
	if (band < 0 || band >= APLAY_EQ_BANDS) return 0.0f;
	return __atomic_load_n(&g_eq_band_tenths[band],
	                       __ATOMIC_RELAXED) * 0.1f;
}

float aplay_equalizer_frequency(int band)
{
	if (band < 0 || band >= APLAY_EQ_BANDS) return 0.0f;
	return g_eq_frequencies[band];
}

void aplay_equalizer_reset(void)
{
	int changed = 0;
	if (__atomic_exchange_n(&g_eq_preamp_tenths, 0,
	                        __ATOMIC_RELAXED) != 0)
		changed = 1;
	for (int i = 0; i < APLAY_EQ_BANDS; ++i) {
		if (__atomic_exchange_n(&g_eq_band_tenths[i], 0,
		                        __ATOMIC_RELAXED) != 0)
			changed = 1;
	}
	if (changed) aplay_eq_touch();
}

typedef struct {
	char name[64];       /* e.g. "hw:7,0" */
	char label[96];      /* e.g. "hw:7,0 (Kazane)" */
	int  card;           /* ALSA card index, or -1 */
	int  pcm;            /* PCM device index on the card, or -1 */
	char card_name[64];  /* human card name, e.g. "Kazane+" */
} AplayDevice;

static AplayDevice g_devices[APLAY_MAX_DEVICES];
static int g_device_count = 0;
/* Set in outr_init / cleared in outr_close; typed after OutRouter is defined. */
static void *g_active_router_ptr = NULL;

static uint64_t aplay_monotonic_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Limit routine progress redraws; control/status changes still render at once. */
static void aplay_render_throttled(tui_state_t *ts, uint64_t *last_render_ns)
{
	uint64_t now;
	if (!ts || !last_render_ns) return;
	now = aplay_monotonic_ns();
	if (now != 0 && *last_render_ns != 0 &&
	    now - *last_render_ns < UI_REFRESH_INTERVAL_NS) return;
	*last_render_ns = now;
	tui_render(ts);
}

// ============================================================
// PCM -> DSD (DoP) real-time output sink
//
// Decoded PCM is fed through dsd_encoder.h to produce a DSD64-equivalent
// bitstream, then sent to ALSA as DoP (DSD over PCM): 2 DSD bytes + 1 marker
// byte packed per 24-bit PCM sample, so the ALSA rate is pcm_rate*OSR/16
// (e.g. 44.1kHz -> 176.4kHz). A DoP-capable DAC plays this as native DSD.
// ============================================================
#ifndef SND_PCM_FORMAT_S24_LE
#define SND_PCM_FORMAT_S24_LE 6
#endif

#define DSD_ENC_OSR 64
#define DSD_ENC_DSD_BYTES_PER_CH \
	(DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8 + 2)
#define DSD_ENC_DOP_FRAMES \
	(DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 16)
#define DSD_ENC_DSD_BYTES_PER_CH_EXACT \
	(DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8)
#define MON_ACCUM_BLOCKS 8

/* Drop queued PCM without waiting for snd_pcm_drain(). */
static void aplay_audio_abort_close(AUDIO *a)
{
	if (!a) return;
	if (a->handle) {
		(void)snd_pcm_drop(a->handle);
		snd_pcm_close(a->handle);
		a->handle = NULL;
	}
	free(a->buffer);
	a->buffer = NULL;
}

/* Discard stale queued audio (seek) but keep the device open. */
static void aplay_audio_discard_queue(AUDIO *a)
{
	if (!a || !a->handle) return;
	(void)snd_pcm_drop(a->handle);
	(void)snd_pcm_prepare(a->handle);
}

typedef struct {
	DSDEncoder *enc;
	AUDIO a;
	int channels;
	uint8_t *dsd_buf;

	/*
	 * Fallback for hardware that cannot open the DoP rate. The generated DSD
	 * is decoded back to PCM and written directly into mon_a.buffer, avoiding
	 * the former intermediate PCM and accumulation buffers.
	 */
	int monitor_mode;
	AUDIO mon_a;
	DSDDecoder *mon_decoder;
	uint8_t *mon_block_buf;
	size_t mon_frames_per_block;
	size_t mon_accum_fill;
	size_t mon_accum_period;
} DsdSink;

static void dsdsink_close(DsdSink *sink);

static int dsdsink_open(DsdSink *sink, const char *dev_name,
                        int pcm_rate, int channels)
{
	memset(sink, 0, sizeof(*sink));
	sink->channels = channels;

	sink->enc = dsd_encoder_init(channels, pcm_rate,
	                             pcm_rate * DSD_ENC_OSR);
	if (!sink->enc) {
		fprintf(stderr,
		        "DSD encoder init failed (channels=%d, rate=%d)\n",
		        channels, pcm_rate);
		return -1;
	}

	sink->dsd_buf = (uint8_t *)malloc(
		(size_t)DSD_ENC_DSD_BYTES_PER_CH * (size_t)channels);
	if (!sink->dsd_buf) goto fail;

	{
		int dop_rate = pcm_rate * DSD_ENC_OSR / 16;
		if (AUDIO_init(&sink->a, (char *)dev_name, dop_rate, channels,
		               DSD_ENC_DOP_FRAMES, 1,
		               SND_PCM_FORMAT_S24_LE) == 0) {
			if (sink->a.freq == (unsigned int)dop_rate) {
				printf("DSD encode (DoP) output: %d Hz PCM -> "
				       "DSD64 @ %d Hz DoP / S24_LE / %dch\n",
				       pcm_rate, dop_rate, channels);
				return 0;
			}
			fprintf(stderr,
			        "DoP: rejected ALSA rate remap %d -> %u Hz\n",
			        dop_rate, sink->a.freq);
			if (sink->a.handle) AUDIO_close(&sink->a);
			memset(&sink->a, 0, sizeof(sink->a));
		}

		fprintf(stderr,
		        "DoP output unavailable at %d Hz, falling back to "
		        "PCM->DSD->PCM monitor mode\n", dop_rate);
	}

	{
		int dsd_rate = pcm_rate * DSD_ENC_OSR;
		static const int mon_rate_prefs[] = {
			384000, 352800, 192000, 176400,
			96000, 88200, 48000, 44100
		};
		int cands[1 + (int)(sizeof(mon_rate_prefs) /
		                           sizeof(mon_rate_prefs[0]))];
		int ncands = 0;
		int mon_rate = 0;

		sink->mon_decoder = dsd_decoder_init_raw(
			channels, dsd_rate, DSD_ENC_DSD_BYTES_PER_CH_EXACT);
		if (!sink->mon_decoder) {
			fprintf(stderr,
			        "Failed to init monitor-mode DSD decoder\n");
			goto fail;
		}

		for (size_t i = 0;
		     i < sizeof(mon_rate_prefs) / sizeof(mon_rate_prefs[0]);
		     ++i) {
			int r = mon_rate_prefs[i];
			int dec;
			if (r == pcm_rate || dsd_rate % r != 0) continue;
			dec = dsd_rate / r;
			if (dec < 8 || dec > 512) continue;
			cands[ncands++] = r;
		}
		cands[ncands++] = pcm_rate;

		for (int i = 0; i < ncands && !mon_rate; ++i) {
			if (dsd_decoder_set_pcm_rate(
				    sink->mon_decoder, cands[i]) != 0) continue;

			sink->mon_frames_per_block =
				dsd_decoder_frames_per_block(sink->mon_decoder);
			if (sink->mon_frames_per_block == 0) continue;

			sink->mon_accum_period =
				sink->mon_frames_per_block * MON_ACCUM_BLOCKS;

			if (AUDIO_init(&sink->mon_a, (char *)dev_name,
			               (unsigned int)cands[i], channels,
			               (int)sink->mon_accum_period, 4,
			               SND_PCM_FORMAT_FLOAT_LE) != 0) continue;

			if (sink->mon_a.freq != (unsigned int)cands[i]) {
				fprintf(stderr,
				        "monitor: rejected ALSA rate remap "
				        "%d -> %u Hz\n",
				        cands[i], sink->mon_a.freq);
				if (sink->mon_a.handle)
					AUDIO_close(&sink->mon_a);
				memset(&sink->mon_a, 0,
				       sizeof(sink->mon_a));
				continue;
			}
			mon_rate = cands[i];
		}

		if (!mon_rate) {
			fprintf(stderr,
			        "Failed to open ALSA device for monitor-mode "
			        "PCM output (source %d Hz and fallbacks)\n",
			        pcm_rate);
			goto fail;
		}

		sink->mon_block_buf = (uint8_t *)malloc(
			(size_t)DSD_ENC_DSD_BYTES_PER_CH_EXACT *
			(size_t)channels);
		if (!sink->mon_block_buf) goto fail;

		sink->mon_accum_fill = 0;
		sink->monitor_mode = 1;
		printf("DSD encode (monitor) output: %d Hz PCM -> DSD64 "
		       "-> %d Hz PCM via dsd.h / %dch\n",
		       pcm_rate, mon_rate, channels);
		return 0;
	}

fail:
	dsdsink_close(sink);
	return -1;
}

static void dsdsink_write(DsdSink *sink,
                          const float *pcm, size_t frames)
{
	size_t bytes_per_ch = 0;
	const uint8_t *ch_ptrs[DSDENC_MAX_CHANNELS];

	if (!sink || !sink->enc || !pcm ||
	    frames != DSD_ENC_INPUT_CHUNK) return;

	if (dsd_encoder_process_raw(
		    sink->enc, pcm, frames, sink->dsd_buf,
		    DSD_ENC_DSD_BYTES_PER_CH * sink->channels,
		    &bytes_per_ch) != 0) return;

	for (int ch = 0; ch < sink->channels; ++ch) {
		ch_ptrs[ch] = sink->dsd_buf +
			(size_t)ch * DSD_ENC_DSD_BYTES_PER_CH;
	}

	if (g_dsd_raw_file) {
		for (int ch = 0; ch < sink->channels; ++ch)
			(void)fwrite(ch_ptrs[ch], 1, bytes_per_ch,
			             g_dsd_raw_file);
	}

	if (sink->monitor_mode) {
		for (int ch = 0; ch < sink->channels; ++ch) {
			memcpy(sink->mon_block_buf +
			       (size_t)ch *
			       DSD_ENC_DSD_BYTES_PER_CH_EXACT,
			       ch_ptrs[ch],
			       DSD_ENC_DSD_BYTES_PER_CH_EXACT);
		}

		dsd_decoder_feed_block(sink->mon_decoder,
		                       sink->mon_block_buf);

		for (;;) {
			size_t room = sink->mon_accum_period -
			              sink->mon_accum_fill;
			size_t out_frames;
			float *dst;

			if (room == 0) {
				AUDIO_play0(&sink->mon_a);
				AUDIO_wait(&sink->mon_a, AUDIO_WAIT_MS);
				sink->mon_accum_fill = 0;
				room = sink->mon_accum_period;
			}

			dst = (float *)sink->mon_a.buffer +
			      sink->mon_accum_fill *
			      (size_t)sink->channels;
			out_frames = dsd_decoder_read_pcm_frames(
				sink->mon_decoder, room, dst,
				SND_PCM_FORMAT_FLOAT_LE);
			if (out_frames == 0) break;

			sink->mon_accum_fill += out_frames;
			if (sink->mon_accum_fill >=
			    sink->mon_accum_period) {
				AUDIO_play0(&sink->mon_a);
				AUDIO_wait(&sink->mon_a, AUDIO_WAIT_MS);
				sink->mon_accum_fill = 0;
			}

			if (out_frames < room) break;
		}
		return;
	}

	{
		size_t dop_frames = 0;
		size_t even_bytes = bytes_per_ch & ~((size_t)1);
		/*
		 * Pack directly into ALSA's buffer. This removes dop_buf and
		 * one full copy for every DSD block.
		 */
		dsd_encoder_pack_dop(
			sink->enc, ch_ptrs, even_bytes,
			(int32_t *)sink->a.buffer, &dop_frames);
		AUDIO_play0(&sink->a);
		AUDIO_wait(&sink->a, AUDIO_WAIT_MS);
	}
}

static void dsdsink_close(DsdSink *sink)
{
	if (!sink) return;

	if (sink->monitor_mode &&
	    sink->mon_accum_fill > 0 &&
	    sink->mon_a.handle && sink->mon_a.buffer) {
		size_t remain = sink->mon_accum_period -
		                sink->mon_accum_fill;
		memset((float *)sink->mon_a.buffer +
		       sink->mon_accum_fill *
		       (size_t)sink->channels,
		       0,
		       remain * (size_t)sink->channels *
		       sizeof(float));
		AUDIO_play0(&sink->mon_a);
		AUDIO_wait(&sink->mon_a, AUDIO_WAIT_MS);
	}
	if (sink->mon_a.handle) AUDIO_close(&sink->mon_a);
	if (sink->a.handle) AUDIO_close(&sink->a);

	if (sink->mon_decoder)
		dsd_decoder_free(sink->mon_decoder);
	if (sink->enc)
		dsd_encoder_free(sink->enc);
	free(sink->mon_block_buf);
	free(sink->dsd_buf);
	memset(sink, 0, sizeof(*sink));
}

/* Immediate DSD teardown for Stop/Next/device-mode changes. */
static void dsdsink_abort(DsdSink *sink)
{
	if (!sink) return;
	aplay_audio_abort_close(&sink->mon_a);
	aplay_audio_abort_close(&sink->a);
	if (sink->mon_decoder) dsd_decoder_free(sink->mon_decoder);
	if (sink->enc) dsd_encoder_free(sink->enc);
	free(sink->mon_block_buf);
	free(sink->dsd_buf);
	memset(sink, 0, sizeof(*sink));
}

// ============================================================
// DsdAccum: keeps only a partial 32-frame DSD block.
//
// Full float blocks are sent to the encoder directly. Therefore large decoder
// blocks no longer pass through an 8192-frame buffer or require memmove().
// ============================================================
typedef struct {
	float *buf;
	size_t fill;
	int channels;
} DsdAccum;

static void dsdaccum_init(DsdAccum *ac, int channels)
{
	memset(ac, 0, sizeof(*ac));
	ac->channels = channels;
}

static int dsdaccum_reserve(DsdAccum *ac)
{
	if (ac->buf) return 0;
	ac->buf = (float *)malloc(
		(size_t)DSD_ENC_INPUT_CHUNK *
		(size_t)ac->channels * sizeof(float));
	return ac->buf ? 0 : -1;
}

static void dsdaccum_reset(DsdAccum *ac)
{
	if (ac) ac->fill = 0;
}

static void dsdaccum_free(DsdAccum *ac)
{
	if (!ac) return;
	free(ac->buf);
	memset(ac, 0, sizeof(*ac));
}

static void dsdaccum_feed_f32(DsdAccum *ac, DsdSink *sink,
                              const float *src, size_t frames)
{
	const size_t chunk = DSD_ENC_INPUT_CHUNK;
	const size_t channels = (size_t)ac->channels;

	if (!src || frames == 0 || dsdaccum_reserve(ac) != 0) return;

	if (ac->fill > 0) {
		size_t take = chunk - ac->fill;
		if (take > frames) take = frames;
		memcpy(ac->buf + ac->fill * channels, src,
		       take * channels * sizeof(float));
		ac->fill += take;
		src += take * channels;
		frames -= take;
		if (ac->fill == chunk) {
			dsdsink_write(sink, ac->buf, chunk);
			ac->fill = 0;
		}
	}

	while (frames >= chunk) {
		dsdsink_write(sink, src, chunk);
		src += chunk * channels;
		frames -= chunk;
	}

	if (frames > 0) {
		memcpy(ac->buf, src,
		       frames * channels * sizeof(float));
		ac->fill = frames;
	}
}

static void dsdaccum_feed_s16(DsdAccum *ac, DsdSink *sink,
                              const int16_t *src, size_t frames)
{
	const size_t chunk = DSD_ENC_INPUT_CHUNK;
	const size_t channels = (size_t)ac->channels;

	if (!src || frames == 0 || dsdaccum_reserve(ac) != 0) return;

	while (frames > 0) {
		size_t room = chunk - ac->fill;
		size_t take = frames < room ? frames : room;
		size_t dst_off = ac->fill * channels;
		size_t samples = take * channels;

		for (size_t i = 0; i < samples; ++i)
			ac->buf[dst_off + i] =
				src[i] * (1.0f / 32768.0f);

		ac->fill += take;
		src += samples;
		frames -= take;

		if (ac->fill == chunk) {
			dsdsink_write(sink, ac->buf, chunk);
			ac->fill = 0;
		}
	}
}

static void dsdaccum_flush(DsdAccum *ac, DsdSink *sink)
{
	size_t channels;
	size_t remain;

	if (!ac || !ac->buf || ac->fill == 0 ||
	    !sink || !sink->enc) return;

	channels = (size_t)ac->channels;
	remain = DSD_ENC_INPUT_CHUNK - ac->fill;
	memset(ac->buf + ac->fill * channels, 0,
	       remain * channels * sizeof(float));
	dsdsink_write(sink, ac->buf, DSD_ENC_INPUT_CHUNK);
	ac->fill = 0;
}

// ============================================================
// OutRouter: routes decoded audio to normal PCM or DSD(DoP), with live
// switching. Conversion and DSP buffers are allocated only when required.
// ============================================================
typedef struct {
	int use_dsd;
	int use_super_res;
	int channels;
	int sample_rate;
	int format;
	char dev_buf[128];

	AUDIO pcm;
	int pcm_open;

	DsdSink dsd;
	int dsd_open;
	DsdAccum accum;

	int16_t *i16_scratch;
	float *s16_f32;
	float *sr_scratch;
	float *sr_prev;
	int sr_have_prev;

	/* Allocated only when the enabled EQ has a non-flat setting. */
	float *eq_scratch;
	float *eq_z1;
	float *eq_z2;
	float eq_b0[APLAY_EQ_BANDS];
	float eq_b1[APLAY_EQ_BANDS];
	float eq_b2[APLAY_EQ_BANDS];
	float eq_a1[APLAY_EQ_BANDS];
	float eq_a2[APLAY_EQ_BANDS];
	float eq_preamp_gain;
	unsigned int eq_revision;
	unsigned int eq_active_mask;
	unsigned char eq_active_idx[APLAY_EQ_BANDS];
	int eq_active_count;
	int eq_processing;
} OutRouter;

static int outr_ensure_i16_scratch(OutRouter *r)
{
	if (r->i16_scratch) return 0;
	r->i16_scratch = (int16_t *)malloc(
		(size_t)AUDIO_SCRATCH_FRAMES *
		(size_t)r->channels * sizeof(int16_t));
	return r->i16_scratch ? 0 : -1;
}

static int outr_ensure_s16_f32(OutRouter *r)
{
	if (r->s16_f32) return 0;
	r->s16_f32 = (float *)malloc(
		(size_t)AUDIO_SCRATCH_FRAMES *
		(size_t)r->channels * sizeof(float));
	return r->s16_f32 ? 0 : -1;
}

static int outr_ensure_super_res(OutRouter *r)
{
	if (!r->sr_scratch) {
		r->sr_scratch = (float *)malloc(
			(size_t)AUDIO_SCRATCH_FRAMES *
			(size_t)r->channels * sizeof(float));
	}
	if (!r->sr_prev) {
		r->sr_prev = (float *)calloc(
			(size_t)r->channels, sizeof(float));
	}
	if (!r->sr_scratch || !r->sr_prev) {
		free(r->sr_scratch);
		free(r->sr_prev);
		r->sr_scratch = NULL;
		r->sr_prev = NULL;
		return -1;
	}
	return 0;
}


static float aplay_clampf(float v);

static int outr_ensure_equalizer(OutRouter *r)
{
	size_t samples, states;
	if (!r) return -1;
	samples = (size_t)AUDIO_SCRATCH_FRAMES * (size_t)r->channels;
	states = (size_t)APLAY_EQ_BANDS * (size_t)r->channels;
	if (!r->eq_scratch)
		r->eq_scratch = (float *)malloc(samples * sizeof(float));
	if (!r->eq_z1)
		r->eq_z1 = (float *)calloc(states, sizeof(float));
	if (!r->eq_z2)
		r->eq_z2 = (float *)calloc(states, sizeof(float));
	if (!r->eq_scratch || !r->eq_z1 || !r->eq_z2) {
		free(r->eq_scratch);
		free(r->eq_z1);
		free(r->eq_z2);
		r->eq_scratch = NULL;
		r->eq_z1 = NULL;
		r->eq_z2 = NULL;
		return -1;
	}
	return 0;
}

/* Refresh only after a GUI-side setting change. Returns whether DSP is needed. */
static int outr_refresh_equalizer(OutRouter *r)
{
	const float q = 1.41421356237f;
	unsigned int revision;
	unsigned int old_mask;
	int old_processing;
	int enabled;
	int preamp_tenths;
	int active_count = 0;
	unsigned int mask = 0;

	if (!r) return 0;
	revision = __atomic_load_n(&g_eq_revision, __ATOMIC_ACQUIRE);
	if (revision == r->eq_revision) return r->eq_processing;

	old_mask = r->eq_active_mask;
	old_processing = r->eq_processing;
	enabled = __atomic_load_n(&g_eq_enabled, __ATOMIC_RELAXED);
	preamp_tenths = __atomic_load_n(&g_eq_preamp_tenths,
	                                __ATOMIC_RELAXED);

	r->eq_revision = revision;
	r->eq_preamp_gain = preamp_tenths == 0
		? 1.0f : powf(10.0f, preamp_tenths * (1.0f / 200.0f));

	for (int band = 0; band < APLAY_EQ_BANDS; ++band) {
		int gain_tenths = __atomic_load_n(&g_eq_band_tenths[band],
		                                  __ATOMIC_RELAXED);
		float frequency = g_eq_frequencies[band];
		if (!enabled || gain_tenths == 0 ||
		    frequency >= (float)r->sample_rate * 0.48f)
			continue;

		float gain_db = gain_tenths * 0.1f;
		float a = powf(10.0f, gain_db * 0.025f);
		float w0 = 6.2831853071795864769f * frequency /
		           (float)r->sample_rate;
		float cw = cosf(w0);
		float alpha = sinf(w0) / (2.0f * q);
		float a0 = 1.0f + alpha / a;
		float inv_a0 = 1.0f / a0;

		r->eq_b0[band] = (1.0f + alpha * a) * inv_a0;
		r->eq_b1[band] = (-2.0f * cw) * inv_a0;
		r->eq_b2[band] = (1.0f - alpha * a) * inv_a0;
		r->eq_a1[band] = (-2.0f * cw) * inv_a0;
		r->eq_a2[band] = (1.0f - alpha / a) * inv_a0;
		r->eq_active_idx[active_count++] = (unsigned char)band;
		mask |= 1u << band;
	}

	r->eq_active_count = active_count;
	r->eq_active_mask = mask;
	r->eq_processing = enabled &&
		(preamp_tenths != 0 || active_count > 0);

	if (!r->eq_processing) return 0;
	if (outr_ensure_equalizer(r) != 0) {
		fprintf(stderr,
		        "Equalizer buffer allocation failed; DSP bypassed\n");
		r->eq_processing = 0;
		return 0;
	}

	/* Starting DSP or changing which filters exist must not reuse unrelated
	 * delay state. Gain-only changes retain state and therefore remain smooth. */
	if (!old_processing || old_mask != mask) {
		size_t states = (size_t)APLAY_EQ_BANDS *
		                (size_t)r->channels;
		memset(r->eq_z1, 0, states * sizeof(float));
		memset(r->eq_z2, 0, states * sizeof(float));
	}
	return 1;
}

static int outr_equalizer_active(OutRouter *r)
{
	unsigned int revision;
	if (!r) return 0;
	revision = __atomic_load_n(&g_eq_revision, __ATOMIC_ACQUIRE);
	if (revision != r->eq_revision)
		return outr_refresh_equalizer(r);
	return r->eq_processing;
}

static const float *outr_apply_equalizer(OutRouter *r,
                                         const float *src,
                                         size_t frames)
{
	size_t samples;
	if (!r || !src || frames == 0) return src;
	if (!outr_equalizer_active(r)) return src;
	if (!r->eq_scratch || !r->eq_z1 || !r->eq_z2) return src;

	samples = frames * (size_t)r->channels;
	if (r->eq_active_count == 0) {
		for (size_t i = 0; i < samples; ++i)
			r->eq_scratch[i] =
				aplay_clampf(src[i] * r->eq_preamp_gain);
		return r->eq_scratch;
	}

	for (size_t frame = 0; frame < frames; ++frame) {
		for (int ch = 0; ch < r->channels; ++ch) {
			size_t p = frame * (size_t)r->channels + (size_t)ch;
			float x = src[p] * r->eq_preamp_gain;
			for (int ai = 0; ai < r->eq_active_count; ++ai) {
				int band = r->eq_active_idx[ai];
				size_t s = (size_t)band *
				           (size_t)r->channels + (size_t)ch;
				float y = r->eq_b0[band] * x + r->eq_z1[s];
				r->eq_z1[s] = r->eq_b1[band] * x -
				              r->eq_a1[band] * y + r->eq_z2[s];
				r->eq_z2[s] = r->eq_b2[band] * x -
				              r->eq_a2[band] * y;
				x = y;
			}
			r->eq_scratch[p] = aplay_clampf(x);
		}
	}

	/* Flush extremely small state once per block, not inside the hot loop. */
	for (int ai = 0; ai < r->eq_active_count; ++ai) {
		int band = r->eq_active_idx[ai];
		for (int ch = 0; ch < r->channels; ++ch) {
			size_t s = (size_t)band *
			           (size_t)r->channels + (size_t)ch;
			if (fabsf(r->eq_z1[s]) < 1.0e-20f) r->eq_z1[s] = 0.0f;
			if (fabsf(r->eq_z2[s]) < 1.0e-20f) r->eq_z2[s] = 0.0f;
		}
	}
	return r->eq_scratch;
}

static void outr_close_dsd(OutRouter *r, int flush)
{
	if (!r->dsd_open) return;
	if (flush) {
		dsdaccum_flush(&r->accum, &r->dsd);
		dsdsink_close(&r->dsd);
	} else {
		dsdsink_abort(&r->dsd);
	}
	r->dsd_open = 0;
	dsdaccum_reset(&r->accum);
}

static void outr_open_pcm(OutRouter *r)
{
	if (r->pcm_open) return;
	if (r->dsd_open) outr_close_dsd(r, 0);
	if (AUDIO_init(&r->pcm, r->dev_buf, r->sample_rate,
	               r->channels, AUDIO_PERIOD_FRAMES, 1, r->format) == 0)
		r->pcm_open = 1;
}

static void outr_open_dsd(OutRouter *r)
{
	if (r->dsd_open) return;

	if (dsdaccum_reserve(&r->accum) != 0) {
		fprintf(stderr,
		        "Failed to allocate DSD staging buffer; "
		        "falling back to normal PCM\n");
		r->use_dsd = 0;
		outr_open_pcm(r);
		return;
	}

	if (r->pcm_open) {
		aplay_audio_abort_close(&r->pcm);
		r->pcm_open = 0;
	}

	if (dsdsink_open(&r->dsd, r->dev_buf,
	                 r->sample_rate, r->channels) == 0) {
		r->dsd_open = 1;
		dsdaccum_reset(&r->accum);
		return;
	}

	fprintf(stderr,
	        "DSD output unavailable; falling back to normal PCM\n");
	r->use_dsd = 0;
	outr_open_pcm(r);
}

static const char *outr_status_note(OutRouter *r)
{
	if (!r->use_dsd) return "PCM output";
	if (!r->dsd_open) return "DSD (pending device)";
	return r->dsd.monitor_mode
		? "DSD monitor (PCM->DSD->PCM, no DoP hw)"
		: "DSD(DoP) output";
}

/* Open the ALSA device now if it is not already open. */
static int outr_ensure_open(OutRouter *r)
{
	if (!r) return -1;

	/* Hard-stop (Tab) may release the underlying handle. */
	if (r->pcm_open && !r->pcm.handle)
		r->pcm_open = 0;

	if (r->dsd_open) {
		AUDIO *da = r->dsd.monitor_mode
			? &r->dsd.mon_a : &r->dsd.a;
		if (!da->handle)
			outr_close_dsd(r, 0);
	}

	if (r->pcm_open || r->dsd_open) return 0;
	if (r->use_dsd) outr_open_dsd(r);
	else outr_open_pcm(r);
	return (r->pcm_open || r->dsd_open) ? 0 : -1;
}

/* Wait for a busy playback device while keeping controls responsive. */
static int outr_wait_open(OutRouter *r, tui_state_t *ts)
{
	int announced = 0;

	for (;;) {
		if (outr_ensure_open(r) == 0) {
			apply_alsa_volume();
			if (ts) {
				ts->paused = 0;
				ts->note = outr_status_note(r);
				tui_render(ts);
			}
			return 0;
		}

		if (!announced) {
			fprintf(stderr,
			        "aplay+: device '%s' busy — waiting "
			        "(will play when free)\n", r->dev_buf);
			announced = 1;
		}

		if (ts) {
			ts->paused = 1;
			ts->note = "Device busy — waiting";
			ts->device = r->dev_buf;
			tui_render(ts);
		}

		{
			int k = key(NULL, ts);
			if (k == 'q' || k == 0x1b ||
			    k == 'n' || k == 'b' || k == 'p' ||
			    k == '\\' || k == 'd' || k == 'A') {
				cmd = k;
				return -1;
			}
		}
		usleep(200000);
	}
}

/* Same retry loop for paths that open AUDIO directly. */
static int audio_wait_init(AUDIO *a, char *devname,
                           unsigned int freq, int ch,
                           int frames, int flag, int format,
                           tui_state_t *ts)
{
	int announced = 0;
	memset(a, 0, sizeof(*a));

	for (;;) {
		if (AUDIO_init(a, devname, freq, ch,
		               frames, flag, format) == 0) {
			apply_alsa_volume();
			if (ts) {
				ts->paused = 0;
				tui_render(ts);
			}
			return 0;
		}

		if (!announced) {
			fprintf(stderr,
			        "aplay+: device '%s' busy — waiting "
			        "(will play when free)\n",
			        devname ? devname : "?");
			announced = 1;
		}

		if (ts) {
			ts->paused = 1;
			ts->note = "Device busy — waiting";
			ts->device = devname;
			tui_render(ts);
		}

		{
			int k = key(NULL, ts);
			if (k == 'q' || k == 0x1b ||
			    k == 'n' || k == 'b' || k == 'p' ||
			    k == '\\' || k == 'd' || k == 'A') {
				cmd = k;
				return -1;
			}
		}
		usleep(200000);
	}
}

static int outr_init(OutRouter *r, const char *dev_name,
                     int sample_rate, int channels, int format,
                     int start_with_dsd,
                     int start_with_super_res)
{
	if (!r || !dev_name || channels <= 0 ||
	    sample_rate <= 0) return -1;

	memset(r, 0, sizeof(*r));
	r->sample_rate = sample_rate;
	r->channels = channels;
	r->format = format;
	snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", dev_name);
	dsdaccum_init(&r->accum, channels);
	r->use_dsd = start_with_dsd ? 1 : 0;
	r->use_super_res = start_with_super_res ? 1 : 0;

	if (r->use_super_res &&
	    outr_ensure_super_res(r) != 0) {
		fprintf(stderr,
		        "Super-resolution buffer allocation failed; "
		        "feature disabled\n");
		r->use_super_res = 0;
	}

	/* ALSA open remains deferred so a busy card does not tear down the UI. */
	g_active_router_ptr = r;
	return 0;
}

static void outr_toggle(OutRouter *r)
{
	r->use_dsd = !r->use_dsd;
	if (r->use_dsd) outr_open_dsd(r);
	else outr_open_pcm(r);
}

static void outr_toggle_super_res(OutRouter *r)
{
	r->use_super_res = !r->use_super_res;
	r->sr_have_prev = 0;

	if (r->use_super_res &&
	    outr_ensure_super_res(r) != 0) {
		fprintf(stderr,
		        "Super-resolution buffer allocation failed; "
		        "feature disabled\n");
		r->use_super_res = 0;
	}
}

static AUDIO *outr_audio(OutRouter *r)
{
	if (!r->use_dsd) return &r->pcm;
	return r->dsd.monitor_mode ? &r->dsd.mon_a : &r->dsd.a;
}

static int outr_output_rate(OutRouter *r)
{
	if (!r->use_dsd || !r->dsd_open) return r->sample_rate;
	if (r->dsd.monitor_mode) return (int)r->dsd.mon_a.freq;
	return (int)r->dsd.a.freq;
}

/* Remove already queued audio after a seek so the new position is audible now. */
static void outr_discard_queued(OutRouter *r)
{
	if (!r) return;
	if (r->pcm_open) aplay_audio_discard_queue(&r->pcm);
	if (r->dsd_open) {
		AUDIO *a = r->dsd.monitor_mode ? &r->dsd.mon_a : &r->dsd.a;
		aplay_audio_discard_queue(a);
		r->dsd.mon_accum_fill = 0;
		dsdaccum_reset(&r->accum);
	}
	r->sr_have_prev = 0;
	if (r->eq_z1 && r->eq_z2) {
		size_t states = (size_t)APLAY_EQ_BANDS * (size_t)r->channels;
		memset(r->eq_z1, 0, states * sizeof(float));
		memset(r->eq_z2, 0, states * sizeof(float));
	}
}

/* Transport controls must not wait for the ALSA drain queue. */
static void outr_abort(OutRouter *r)
{
	if (!r) return;
	if (r->pcm_open || r->pcm.handle || r->pcm.buffer) {
		aplay_audio_abort_close(&r->pcm);
		r->pcm_open = 0;
	}
	if (r->dsd_open) outr_close_dsd(r, 0);
	r->sr_have_prev = 0;
}

static void outr_close(OutRouter *r)
{
	if (!r) return;
	if (g_active_router_ptr == r) g_active_router_ptr = NULL;
	if (r->pcm_open || r->pcm.handle || r->pcm.buffer) AUDIO_close(&r->pcm);
	outr_close_dsd(r, 1);
	dsdaccum_free(&r->accum);
	free(r->i16_scratch);
	free(r->s16_f32);
	free(r->sr_scratch);
	free(r->sr_prev);
	free(r->eq_scratch);
	free(r->eq_z1);
	free(r->eq_z2);
	memset(r, 0, sizeof(*r));
}

/* Live-switch the ALSA device while preserving PCM/DSD mode. */
static int outr_set_device(OutRouter *r, const char *name)
{
	char prev[sizeof(r->dev_buf)];
	int was_dsd;

	if (!r || !name || !name[0]) return -1;
	snprintf(prev, sizeof(prev), "%s", r->dev_buf);
	was_dsd = r->use_dsd;
	snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", name);

	if (r->pcm_open || r->pcm.handle || r->pcm.buffer) {
		aplay_audio_abort_close(&r->pcm);
		r->pcm_open = 0;
	}
	outr_close_dsd(r, 0);

	r->use_dsd = was_dsd;
	if (was_dsd) outr_open_dsd(r);
	else outr_open_pcm(r);

	if (!r->pcm_open && !r->dsd_open) {
		snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", prev);
		r->use_dsd = was_dsd;
		if (was_dsd) outr_open_dsd(r);
		else outr_open_pcm(r);
		return -1;
	}

	apply_alsa_volume();
	return 0;
}

static float aplay_clampf(float v)
{
	if (v > 1.0f) return 1.0f;
	if (v < -1.0f) return -1.0f;
	return v;
}

static void outr_feed_float(OutRouter *r,
                            const float *pcm, size_t frames)
{
	int eq_active;

	if (!r || !pcm || frames == 0) return;

	while (frames > AUDIO_SCRATCH_FRAMES) {
		outr_feed_float(r, pcm, AUDIO_SCRATCH_FRAMES);
		pcm += (size_t)AUDIO_SCRATCH_FRAMES *
		       (size_t)r->channels;
		frames -= AUDIO_SCRATCH_FRAMES;
	}

	if (outr_ensure_open(r) < 0) return;

	eq_active = outr_equalizer_active(r);

	/*
	 * Transparent float fast path. With EQ, super resolution and DSD all off,
	 * pass the decoder buffer straight to ALSA without copying, clipping,
	 * gain adjustment, allocation or format conversion.
	 */
	if (!eq_active && !r->use_dsd && !r->use_super_res &&
	    r->format == SND_PCM_FORMAT_FLOAT_LE) {
		AUDIO_play(&r->pcm, (char *)pcm, (int)frames);
		AUDIO_wait(&r->pcm, AUDIO_WAIT_MS);
		return;
	}

	if (eq_active)
		pcm = outr_apply_equalizer(r, pcm, frames);

	if (r->use_super_res) {
		if (outr_ensure_super_res(r) != 0) {
			r->use_super_res = 0;
			r->sr_have_prev = 0;
		} else {
			for (size_t i = 0; i < frames; ++i) {
				for (int ch = 0; ch < r->channels; ++ch) {
					size_t p =
						i * (size_t)r->channels +
						(size_t)ch;
					float cur = pcm[p];
					float prev = i > 0
						? pcm[p - (size_t)r->channels]
						: (r->sr_have_prev
						   ? r->sr_prev[ch] : cur);
					float next = i + 1 < frames
						? pcm[p + (size_t)r->channels]
						: cur;
					r->sr_scratch[p] = aplay_clampf(
						cur + cur -
						0.5f * (prev + next));
				}
			}
			memcpy(r->sr_prev,
			       pcm + (frames - 1) *
			       (size_t)r->channels,
			       (size_t)r->channels *
			       sizeof(float));
			r->sr_have_prev = 1;
			pcm = r->sr_scratch;
		}
	}

	if (r->use_dsd) {
		dsdaccum_feed_f32(&r->accum, &r->dsd,
		                  pcm, frames);
		return;
	}

	if (r->format == SND_PCM_FORMAT_FLOAT_LE) {
		AUDIO_play(&r->pcm, (char *)pcm, (int)frames);
	} else {
		size_t samples = frames * (size_t)r->channels;
		if (outr_ensure_i16_scratch(r) != 0) return;
		for (size_t i = 0; i < samples; ++i) {
			float v = pcm[i] * 32768.0f;
			if (v >= 32767.0f) r->i16_scratch[i] = 32767;
			else if (v <= -32768.0f)
				r->i16_scratch[i] = -32768;
			else r->i16_scratch[i] = (int16_t)v;
		}
		AUDIO_play(&r->pcm, (char *)r->i16_scratch,
		           (int)frames);
	}
	AUDIO_wait(&r->pcm, AUDIO_WAIT_MS);
}

static void outr_feed_s16(OutRouter *r,
                          const int16_t *pcm, size_t frames)
{
	if (!r || !pcm || frames == 0) return;

	while (frames > AUDIO_SCRATCH_FRAMES) {
		outr_feed_s16(r, pcm, AUDIO_SCRATCH_FRAMES);
		pcm += (size_t)AUDIO_SCRATCH_FRAMES *
		       (size_t)r->channels;
		frames -= AUDIO_SCRATCH_FRAMES;
	}

	if (outr_ensure_open(r) < 0) return;

	int eq_active = outr_equalizer_active(r);

	/*
	 * Fast path: decoded S16 goes directly to ALSA. This avoids the former
	 * S16 -> float -> S16 round trip for OGG/WMA/AAC/minimp3.
	 */
	if (!eq_active && !r->use_dsd && !r->use_super_res &&
	    r->format != SND_PCM_FORMAT_FLOAT_LE) {
		AUDIO_play(&r->pcm, (char *)pcm, (int)frames);
		AUDIO_wait(&r->pcm, AUDIO_WAIT_MS);
		return;
	}

	/* DSD without float DSP converts only the encoder's 32-frame block. */
	if (!eq_active && r->use_dsd && !r->use_super_res) {
		dsdaccum_feed_s16(&r->accum, &r->dsd,
		                  pcm, frames);
		return;
	}

	if (outr_ensure_s16_f32(r) != 0) return;
	{
		size_t samples = frames * (size_t)r->channels;
		for (size_t i = 0; i < samples; ++i)
			r->s16_f32[i] =
				pcm[i] * (1.0f / 32768.0f);
	}
	outr_feed_float(r, r->s16_f32, frames);
}

// ============================================================
// CrosstalkCancel: loudspeaker crosstalk cancellation
// ============================================================
typedef struct {
	int delay_samples;
	float attenuation;
	float *delay_buffer;
	int delay_buffer_size;
	int delay_index;
} CrosstalkCancel;

static void init_crosstalk_cancellation(
	CrosstalkCancel *xtc, int sample_rate, int channels)
{
	if (!xtc) return;
	memset(xtc, 0, sizeof(*xtc));

	if (channels != 2 || sample_rate <= 0) return;

	/* Sound speed: approximately 343 m/s. Allocate lazily on first use. */
	xtc->delay_samples =
		(int)(sample_rate * (speaker_distance_m / 343.0f));
	if (xtc->delay_samples < 1) xtc->delay_samples = 1;
	xtc->delay_buffer_size = xtc->delay_samples * 2;
	xtc->attenuation = xtc_attenuation;
}

static int ensure_crosstalk_buffer(CrosstalkCancel *xtc)
{
	if (!xtc || xtc->delay_buffer_size < 2) return -1;
	if (xtc->delay_buffer) return 0;

	xtc->delay_buffer = (float *)calloc(
		(size_t)xtc->delay_buffer_size, sizeof(float));
	xtc->delay_index = 0;
	return xtc->delay_buffer ? 0 : -1;
}

static void free_crosstalk_cancellation(CrosstalkCancel *xtc)
{
	if (!xtc) return;
	free(xtc->delay_buffer);
	memset(xtc, 0, sizeof(*xtc));
}

static int16_t aplay_float_to_s16(float v)
{
	v = aplay_clampf(v);
	if (v >= 32767.0f / 32768.0f) return 32767;
	if (v <= -1.0f) return -32768;
	return (int16_t)(v * 32768.0f);
}

static void apply_crosstalk_cancellation(
	CrosstalkCancel *xtc, void *buffer,
	int frames, int channels, int format)
{
	if (!xtc || !buffer || frames <= 0 || channels != 2) return;
	if (ensure_crosstalk_buffer(xtc) != 0) return;

	xtc->attenuation = xtc_attenuation;

	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		float *data = (float *)buffer;

		for (int i = 0; i < frames; ++i) {
			int idx = i * 2;
			float left = data[idx];
			float right = data[idx + 1];
			float delayed_left =
				xtc->delay_buffer[xtc->delay_index];
			float delayed_right =
				xtc->delay_buffer[xtc->delay_index + 1];

			/*
			 * Read the delayed frame before overwriting it. The old code
			 * wrote first and therefore often cancelled with the current
			 * sample instead of the delayed sample.
			 */
			xtc->delay_buffer[xtc->delay_index] = left;
			xtc->delay_buffer[xtc->delay_index + 1] = right;
			xtc->delay_index += 2;
			if (xtc->delay_index >= xtc->delay_buffer_size)
				xtc->delay_index = 0;

			data[idx] = aplay_clampf(
				left - xtc->attenuation * delayed_right);
			data[idx + 1] = aplay_clampf(
				right - xtc->attenuation * delayed_left);
		}
	} else {
		int16_t *data = (int16_t *)buffer;

		for (int i = 0; i < frames; ++i) {
			int idx = i * 2;
			float left = data[idx] * (1.0f / 32768.0f);
			float right = data[idx + 1] * (1.0f / 32768.0f);
			float delayed_left =
				xtc->delay_buffer[xtc->delay_index];
			float delayed_right =
				xtc->delay_buffer[xtc->delay_index + 1];

			xtc->delay_buffer[xtc->delay_index] = left;
			xtc->delay_buffer[xtc->delay_index + 1] = right;
			xtc->delay_index += 2;
			if (xtc->delay_index >= xtc->delay_buffer_size)
				xtc->delay_index = 0;

			data[idx] = aplay_float_to_s16(
				left - xtc->attenuation * delayed_right);
			data[idx + 1] = aplay_float_to_s16(
				right - xtc->attenuation * delayed_left);
		}
	}
}

// ============================================================
// Helpers
// ============================================================

static const char *get_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void get_dirpart(const char *path, char *dir, size_t size)
{
	strncpy(dir, path, size - 1);
	dir[size - 1] = '\0';
	char *slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	else strcpy(dir, ".");
}

void apply_alsa_volume(void)
{
	AUDIO_set_volume(dev, volume);
}

void display_progress(uint64_t current, uint64_t total, int is_time, const char *label)
{
	if (total == 0) return;
	if (is_time) {
		int cur_sec = (int)(current % 60);
		int cur_min = (int)(current / 60);
		int tot_sec = (int)(total % 60);
		int tot_min = (int)(total / 60);
		printf("\r%s %02d:%02d / %02d:%02d", label, cur_min, cur_sec, tot_min, tot_sec);
	} else {
		printf("\r%s %llu / %llu bytes", label, (unsigned long long)current, (unsigned long long)total);
	}
	fflush(stdout);
}

void print_test_mode_status(int phase, int flag, CrosstalkCancel *xtc, int sample_rate, int channels)
{
	static const char *phase_name[3] = {"Left channel", "Right channel", "Panning L->R"};
	tui_state_t ts = {0};
	ts.filename = phase_name[phase];
	ts.codec = "TEST";
	ts.rate = sample_rate;
	ts.channels = channels;
	ts.device = dev;
	ts.use_time = 0;
	ts.unit = "";
	ts.volume = volume;
	ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
	ts.xtc_atten = xtc_attenuation;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;
	ts.note = "[+/-] adjust attenuation";
	tui_render(&ts);
}

void play_test_mode(int format, int flag)
{
	const int sample_rate = 44100;
	const int channels = 2;
	const double duration = 5.0;
	const int frames_per_phase = (int)(sample_rate * duration);
	const int total_phases = 3;
	const int frequency = 400;

	AUDIO a;
	tui_state_t ts = {0};
	ts.device = dev;
	ts.note = "Device busy — waiting";
	tui_open();
	if (audio_wait_init(&a, dev, sample_rate, channels, AUDIO_PERIOD_FRAMES, 1, format, &ts) < 0) {
		tui_close();
		return;
	}

	printf("Entering Test mode...\n");
	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		printf("Format: 32-bit FLOAT\n");
	} else {
		printf("Format: 16-bit S16_LE\n");
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, sample_rate, channels);

	uint64_t global_sample_index = 0;
	int phase_sample_index = 0;
	int phase = 0;
	print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
	while (1) {
		int k = key(&a, NULL);
		if (k) {
			if (k == 'q' || k == 0x1b) { aplay_audio_abort_close(&a); break; }
			if (k == 'c') {
				flag ^= USE_CROSSTALK;
				g_flag_diff ^= USE_CROSSTALK;
			}
			if (k == '+' || k == '=') {
				xtc_attenuation += XTC_ATTEN_STEP;
				if (xtc_attenuation > 1.0f) xtc_attenuation = 1.0f;
				xtc.attenuation = xtc_attenuation;
			}
			if (k == '-' || k == '_') {
				xtc_attenuation -= XTC_ATTEN_STEP;
				if (xtc_attenuation < 0.0f) xtc_attenuation = 0.0f;
				xtc.attenuation = xtc_attenuation;
			}
			print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
		}

		void *output_buffer = a.buffer;
		float *buffer_f32 = (float*)a.buffer;
		int16_t *buffer_s16 = (int16_t*)a.buffer;
		for (int i = 0; i < AUDIO_PERIOD_FRAMES; i++) {
			double t = (double)(global_sample_index + i) / sample_rate;
			int current_phase_sample = phase_sample_index + i;
			double left_amplitude, right_amplitude;

			if (phase == 0) {
				left_amplitude = 0.5; right_amplitude = 0.0;
			} else if (phase == 1) {
				left_amplitude = 0.0; right_amplitude = 0.5;
			} else {
				double pan = (double)current_phase_sample / frames_per_phase;
				if (pan > 1.0) pan = 1.0;
				left_amplitude  = 0.5 * (1.0 - pan);
				right_amplitude = 0.5 * pan;
			}

			double wave = sin(2.0 * M_PI * frequency * t);
			double left  = left_amplitude * wave;
			double right = right_amplitude * wave;

			if (format == SND_PCM_FORMAT_FLOAT_LE) {
				buffer_f32[i * 2]     = (float)left;
				buffer_f32[i * 2 + 1] = (float)right;
			} else {
				buffer_s16[i * 2]     = (int16_t)(left  * 32767.0f);
				buffer_s16[i * 2 + 1] = (int16_t)(right * 32767.0f);
			}
		}

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, output_buffer, AUDIO_PERIOD_FRAMES, channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, AUDIO_WAIT_MS);

		global_sample_index += AUDIO_PERIOD_FRAMES;
		phase_sample_index  += AUDIO_PERIOD_FRAMES;
		if (phase_sample_index >= frames_per_phase) {
			phase_sample_index = 0;
			phase = (phase + 1) % total_phases;
			print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
		}
	}
	tui_close();

	AUDIO_close(&a);
	free_crosstalk_cancellation(&xtc);
}

// ============================================================
// Format decoders
// ============================================================

void play_wav(char *name, int format, int flag)
{
	drwav wav;
	if (!drwav_init_file(&wav, name, NULL)) return;

	OutRouter router;
	if (outr_init(&router, dev, wav.sampleRate, wav.channels, format,
	              (flag & USE_DSD_ENCODE) ? 1 : 0,
	              (flag & USE_SUPER_RES) ? 1 : 0)) {
		drwav_uninit(&wav);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, wav.sampleRate, wav.channels);

	const int float_output = format == SND_PCM_FORMAT_FLOAT_LE;
	size_t sample_bytes = float_output ? sizeof(float) : sizeof(int16_t);
	void *pcm_buf = malloc((size_t)FRAMES * wav.channels * sample_bytes);
	if (!pcm_buf) {
		outr_close(&router);
		drwav_uninit(&wav);
		free_crosstalk_cancellation(&xtc);
		return;
	}

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd
		? (router.dsd.monitor_mode ? "WAV->DSD(mon)" : "WAV->DSD(DoP)")
		: (float_output ? "WAV/F32" : "WAV");
	ts.rate = outr_output_rate(&router);
	ts.bits = float_output ? 32 : 16;
	ts.channels = wav.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = wav.totalPCMFrameCount > 0
		? (double)wav.totalPCMFrameCount / wav.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		free(pcm_buf);
		outr_close(&router);
		drwav_uninit(&wav);
		free_crosstalk_cancellation(&xtc);
		tui_close();
		return;
	}

	for (;;) {
		size_t n = float_output
			? drwav_read_pcm_frames_f32(&wav, FRAMES, (float *)pcm_buf)
			: drwav_read_pcm_frames_s16(&wav, FRAMES, (int16_t *)pcm_buf);
		if (n == 0) break;

		/* Leave decoded samples untouched unless XTC was explicitly enabled. */
		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(
				&xtc, pcm_buf, (int)n, wav.channels, format);

		if (float_output)
			outr_feed_float(&router, (const float *)pcm_buf, n);
		else
			outr_feed_s16(&router, (const int16_t *)pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES;
			g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res
				? "Super resolution on" : "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE;
			g_flag_diff ^= USE_DSD_ENCODE;
			ts.codec = router.use_dsd
				? (router.dsd.monitor_mode
				   ? "WAV->DSD(mon)" : "WAV->DSD(DoP)")
				: (float_output ? "WAV/F32" : "WAV");
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * wav.sampleRate *
			                (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (wav.totalPCMFrameCount > 0 &&
			    (uint64_t)target > wav.totalPCMFrameCount)
				target = (int64_t)wav.totalPCMFrameCount;
			if (drwav_seek_to_pcm_frame(&wav, (drwav_uint64)target)) {
				outr_discard_queued(&router);
				c = (uint64_t)target;
				ts.cur = (double)c / wav.sampleRate;
				ts.note = k == KEY_RIGHT ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			outr_abort(&router);
			break;
		}

		c += n;
		ts.cur = (double)c / wav.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		ts.note = (n != FRAMES) ? "! short read" : NULL;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drwav_uninit(&wav);
	free_crosstalk_cancellation(&xtc);
}

#ifdef USE_FOXEN_FLAC

#define FX_IN_BUF_SIZE  8192
#define FX_OUT_SAMPLES  (FLAC_SUBSET_MAX_BLOCK_SIZE * FLAC_MAX_CHANNEL_COUNT)

static size_t fx_fill_input(FILE *f, uint8_t *in_buf,
                            uint32_t *in_pos, uint32_t *in_fill)
{
	if (*in_pos == *in_fill) {
		*in_pos = 0;
		*in_fill = 0;
	} else if (*in_fill == FX_IN_BUF_SIZE && *in_pos > 0) {
		uint32_t remain = *in_fill - *in_pos;
		memmove(in_buf, in_buf + *in_pos, remain);
		*in_pos = 0;
		*in_fill = remain;
	}

	if (*in_fill < FX_IN_BUF_SIZE && !feof(f)) {
		size_t rd = fread(in_buf + *in_fill, 1,
		                  FX_IN_BUF_SIZE - *in_fill, f);
		*in_fill += (uint32_t)rd;
	}
	return (size_t)(*in_fill - *in_pos);
}

/*
 * Rewind and decode up to target_frame. Any samples decoded beyond the exact
 * target are retained in pcm_acc, and unconsumed compressed bytes remain in
 * in_buf. This avoids both repeated buffer shifting and seek read-ahead loss.
 */
static int fx_seek_to_frame(
	FILE *f, fx_flac_t *flac, uint64_t target_frame,
	uint32_t channels, uint8_t *in_buf,
	uint32_t *in_pos, uint32_t *in_fill,
	int32_t *out_buf, int32_t *pcm_acc,
	size_t pcm_capacity, size_t *acc_pos, size_t *acc_fill)
{
	uint64_t target_samples = target_frame * channels;
	uint64_t discarded = 0;
	fx_flac_state_t state = FLAC_INIT;

	rewind(f);
	clearerr(f);
	fx_flac_reset(flac);
	*in_pos = 0;
	*in_fill = 0;
	*acc_pos = 0;
	*acc_fill = 0;

	while (discarded < target_samples) {
		uint32_t available;
		uint32_t consumed;
		uint32_t out_len = FX_OUT_SAMPLES;

		if (fx_fill_input(f, in_buf, in_pos, in_fill) == 0)
			break;

		available = *in_fill - *in_pos;
		consumed = available;
		state = fx_flac_process(
			flac, in_buf + *in_pos, &consumed,
			out_buf, &out_len);
		*in_pos += consumed;
		if (*in_pos == *in_fill) {
			*in_pos = 0;
			*in_fill = 0;
		}

		if (state == FLAC_ERR) return 0;

		if (out_len > 0) {
			if (discarded + out_len <= target_samples) {
				discarded += out_len;
			} else {
				size_t skip =
					(size_t)(target_samples - discarded);
				size_t remain = (size_t)out_len - skip;
				if (remain > pcm_capacity) return 0;
				memcpy(pcm_acc, out_buf + skip,
				       remain * sizeof(int32_t));
				*acc_fill = remain;
				discarded = target_samples;
			}
		}

		if (consumed == 0 && out_len == 0) break;
	}

	return state != FLAC_ERR && discarded >= target_samples;
}

void play_flac(char *name, int format, int flag)
{
	FILE *f = NULL;
	fx_flac_t *flac = NULL;
	int32_t *out_buf = NULL;
	int32_t *pcm_acc = NULL;
	void *pcm_out = NULL;
	OutRouter router;
	CrosstalkCancel xtc;
	int router_inited = 0;
	int xtc_inited = 0;
	int tui_is_open = 0;

	uint8_t in_buf[FX_IN_BUF_SIZE];
	uint32_t in_pos = 0;
	uint32_t in_fill = 0;
	fx_flac_state_t state = FLAC_INIT;

	f = fopen(name, "rb");
	if (!f) {
		fprintf(stderr, "Failed to open FLAC: %s\n", name);
		return;
	}

	flac = FX_FLAC_ALLOC_DEFAULT();
	if (!flac) goto done;
	fx_flac_reset(flac);

	out_buf = (int32_t *)malloc(
		(size_t)FX_OUT_SAMPLES * sizeof(int32_t));
	if (!out_buf) goto done;

	/* Parse metadata without moving the remaining compressed bytes. */
	while (state < FLAC_END_OF_METADATA) {
		uint32_t available;
		uint32_t consumed;
		uint32_t out_len = FX_OUT_SAMPLES;

		if (fx_fill_input(f, in_buf, &in_pos, &in_fill) == 0) {
			fprintf(stderr, "FLAC: EOF before metadata: %s\n", name);
			goto done;
		}

		available = in_fill - in_pos;
		consumed = available;
		state = fx_flac_process(
			flac, in_buf + in_pos, &consumed,
			out_buf, &out_len);
		in_pos += consumed;
		if (in_pos == in_fill) in_pos = in_fill = 0;

		if (state == FLAC_ERR) {
			fprintf(stderr, "FLAC: metadata error: %s\n", name);
			goto done;
		}
		if (consumed == 0 && out_len == 0) {
			fprintf(stderr, "FLAC: decoder stalled: %s\n", name);
			goto done;
		}
	}

	{
		uint32_t sample_rate = (uint32_t)fx_flac_get_streaminfo(
			flac, FLAC_KEY_SAMPLE_RATE);
		uint32_t channels = (uint32_t)fx_flac_get_streaminfo(
			flac, FLAC_KEY_N_CHANNELS);
		int64_t n_samples = fx_flac_get_streaminfo(
			flac, FLAC_KEY_N_SAMPLES);
		size_t frames_ch = (size_t)FRAMES * channels;
		size_t pcm_capacity =
			(size_t)FX_OUT_SAMPLES + frames_ch;
		size_t acc_pos = 0;
		size_t acc_fill = 0;
		int eof = 0;
		uint64_t frame_pos = 0;
		uint64_t last_render_ns = 0;
		tui_state_t ts = {0};

		if (sample_rate == 0 || channels == 0) {
			fprintf(stderr, "FLAC: invalid stream info: %s\n", name);
			goto done;
		}

		if (outr_init(&router, dev, (int)sample_rate,
		              (int)channels, format,
		              (flag & USE_DSD_ENCODE) != 0,
		              (flag & USE_SUPER_RES) != 0) != 0)
			goto done;
		router_inited = 1;

		init_crosstalk_cancellation(
			&xtc, (int)sample_rate, (int)channels);
		xtc_inited = 1;

		pcm_acc = (int32_t *)malloc(
			pcm_capacity * sizeof(int32_t));
		pcm_out = malloc(frames_ch *
			(format == SND_PCM_FORMAT_FLOAT_LE
			 ? sizeof(float) : sizeof(int16_t)));
		if (!pcm_acc || !pcm_out) goto done;

		ts.track_index = track_index;
		ts.track_total = track_total;
		ts.filename = get_basename(name);
		char dirbuf[PATH_MAX];
		get_dirpart(name, dirbuf, sizeof(dirbuf));
		ts.dir = dirbuf;
		ts.codec = router.use_dsd
			? "FLAC->DSD(DoP)" : "FLAC";
		ts.rate = outr_output_rate(&router);
		ts.bits = format == SND_PCM_FORMAT_FLOAT_LE ? 32 : 16;
		ts.channels = (int)channels;
		ts.device = dev;
		ts.use_time = 1;
		ts.total = n_samples > 0
			? (double)n_samples / sample_rate : 0.0;
		ts.volume = volume;
		ts.loop_mode = loop_mode;
		ts.format_filter = fmt_filter;

		tui_open();
		tui_is_open = 1;
		if (outr_wait_open(&router, &ts) < 0) goto done;
		ts.codec = router.use_dsd
			? (router.dsd.monitor_mode
			   ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)")
			: "FLAC";

		for (;;) {
			size_t available_samples;

			while (!eof &&
			       acc_fill - acc_pos < frames_ch) {
				uint32_t available;
				uint32_t consumed;
				uint32_t out_len = FX_OUT_SAMPLES;

				if (fx_fill_input(
					    f, in_buf, &in_pos, &in_fill) == 0) {
					eof = 1;
					break;
				}

				available = in_fill - in_pos;
				consumed = available;
				state = fx_flac_process(
					flac, in_buf + in_pos, &consumed,
					out_buf, &out_len);
				in_pos += consumed;
				if (in_pos == in_fill)
					in_pos = in_fill = 0;

				if (state == FLAC_ERR) {
					eof = 1;
					break;
				}

				if (out_len > 0) {
					size_t used = acc_fill - acc_pos;
					if (pcm_capacity - acc_fill <
					    (size_t)out_len) {
						if (acc_pos > 0) {
							memmove(
								pcm_acc,
								pcm_acc + acc_pos,
								used *
								sizeof(int32_t));
							acc_pos = 0;
							acc_fill = used;
						}
					}
					if (pcm_capacity - acc_fill <
					    (size_t)out_len) {
						fprintf(stderr,
						        "FLAC: PCM accumulator "
						        "overflow\n");
						eof = 1;
						break;
					}
					memcpy(pcm_acc + acc_fill, out_buf,
					       (size_t)out_len *
					       sizeof(int32_t));
					acc_fill += out_len;
				}

				if (consumed == 0 && out_len == 0) {
					if (feof(f)) eof = 1;
					break;
				}
			}

			available_samples = acc_fill - acc_pos;
			if (available_samples < channels) {
				if (eof) break;
				continue;
			}

			{
				size_t frames_now =
					available_samples / channels;
				size_t samples_now;
				int k;

				if (frames_now > FRAMES)
					frames_now = FRAMES;
				samples_now = frames_now * channels;

				if (format == SND_PCM_FORMAT_FLOAT_LE) {
					float *pcm_f32 = (float *)pcm_out;
					for (size_t i = 0; i < samples_now; ++i)
						pcm_f32[i] =
							(float)pcm_acc[acc_pos + i] *
							(1.0f / 2147483648.0f);

					if (flag & USE_CROSSTALK)
						apply_crosstalk_cancellation(
							&xtc, pcm_f32,
							(int)frames_now,
							(int)channels,
							SND_PCM_FORMAT_FLOAT_LE);

					outr_feed_float(
						&router, pcm_f32, frames_now);
				} else {
					int16_t *pcm_s16 = (int16_t *)pcm_out;
					for (size_t i = 0; i < samples_now; ++i)
						pcm_s16[i] = (int16_t)(
							pcm_acc[acc_pos + i] / 65536);

					if (flag & USE_CROSSTALK)
						apply_crosstalk_cancellation(
							&xtc, pcm_s16,
							(int)frames_now,
							(int)channels, 0);

					outr_feed_s16(
						&router, pcm_s16, frames_now);
				}

				acc_pos += samples_now;
				if (acc_pos == acc_fill)
					acc_pos = acc_fill = 0;

				k = key(outr_audio(&router), &ts);
				if (k == 'c') {
					flag ^= USE_CROSSTALK;
					g_flag_diff ^= USE_CROSSTALK;
				} else if (k == 's') {
					outr_toggle_super_res(&router);
					flag ^= USE_SUPER_RES;
					g_flag_diff ^= USE_SUPER_RES;
					ts.note = router.use_super_res
						? "Super resolution on"
						: "Super resolution off";
					tui_render(&ts);
				} else if (k == 'e') {
					outr_toggle(&router);
					flag ^= USE_DSD_ENCODE;
					g_flag_diff ^= USE_DSD_ENCODE;
					ts.codec = router.use_dsd
						? (router.dsd.monitor_mode
						   ? "FLAC->DSD(mon)"
						   : "FLAC->DSD(DoP)")
						: "FLAC";
					ts.rate = outr_output_rate(&router);
					ts.note = outr_status_note(&router);
					tui_render(&ts);
				} else if (k == KEY_LEFT ||
				           k == KEY_RIGHT) {
					int64_t delta =
						(int64_t)SEEK_SECONDS *
						sample_rate *
						(k == KEY_RIGHT ? 1 : -1);
					int64_t target =
						(int64_t)frame_pos + delta;
					if (target < 0) target = 0;
					if (n_samples > 0 &&
					    target > n_samples)
						target = n_samples;

					if (fx_seek_to_frame(
						    f, flac,
						    (uint64_t)target,
						    channels, in_buf,
						    &in_pos, &in_fill,
						    out_buf, pcm_acc,
						    pcm_capacity,
						    &acc_pos, &acc_fill)) {
						outr_discard_queued(&router);
						frame_pos =
							(uint64_t)target;
						eof = 0;
						ts.cur =
							(double)frame_pos /
							sample_rate;
						ts.note =
							k == KEY_RIGHT
							? ">> +10s"
							: "<< -10s";
						tui_render(&ts);
					}
				} else if (k) {
					outr_abort(&router);
					break;
				}

				frame_pos += frames_now;
				ts.cur = (double)frame_pos / sample_rate;
				ts.xtc_on =
					(flag & USE_CROSSTALK) != 0;
				ts.xtc_atten = xtc_attenuation;
				aplay_render_throttled(
					&ts, &last_render_ns);
			}
		}
	}

done:
	if (tui_is_open) tui_close();
	free(pcm_acc);
	free(pcm_out);
	if (router_inited) outr_close(&router);
	if (xtc_inited) free_crosstalk_cancellation(&xtc);
	free(out_buf);
	free(flac);
	if (f) fclose(f);
}

#else  /* !USE_FOXEN_FLAC — use dr_flac */

void play_flac(char *name, int format, int flag)
{
	drflac *flac = drflac_open_file(name, NULL);
	if (!flac) {
		fprintf(stderr, "Failed to open FLAC: %s\n", name);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, flac->sampleRate, flac->channels, format,
	              (flag & USE_DSD_ENCODE) ? 1 : 0,
	              (flag & USE_SUPER_RES) ? 1 : 0)) {
		drflac_close(flac);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, flac->sampleRate, flac->channels);

	const int float_output = format == SND_PCM_FORMAT_FLOAT_LE;
	size_t sample_bytes = float_output ? sizeof(float) : sizeof(int16_t);
	void *pcm_buf = malloc((size_t)FRAMES * flac->channels * sample_bytes);
	if (!pcm_buf) {
		outr_close(&router);
		drflac_close(flac);
		free_crosstalk_cancellation(&xtc);
		return;
	}

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd
		? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)")
		: "FLAC";
	ts.rate = outr_output_rate(&router);
	ts.bits = float_output ? 32 : 16;
	ts.channels = flac->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = flac->totalPCMFrameCount > 0
		? (double)flac->totalPCMFrameCount / flac->sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		free(pcm_buf);
		outr_close(&router);
		drflac_close(flac);
		free_crosstalk_cancellation(&xtc);
		tui_close();
		return;
	}

	for (;;) {
		size_t n = float_output
			? drflac_read_pcm_frames_f32(flac, FRAMES, (float *)pcm_buf)
			: drflac_read_pcm_frames_s16(flac, FRAMES, (int16_t *)pcm_buf);
		if (n == 0) break;

		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(
				&xtc, pcm_buf, (int)n, flac->channels, format);

		if (float_output)
			outr_feed_float(&router, (const float *)pcm_buf, n);
		else
			outr_feed_s16(&router, (const int16_t *)pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES;
			g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res
				? "Super resolution on" : "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE;
			g_flag_diff ^= USE_DSD_ENCODE;
			ts.codec = router.use_dsd
				? (router.dsd.monitor_mode
				   ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)")
				: "FLAC";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * flac->sampleRate *
			                (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (flac->totalPCMFrameCount > 0 &&
			    (uint64_t)target > flac->totalPCMFrameCount)
				target = (int64_t)flac->totalPCMFrameCount;
			if (drflac_seek_to_pcm_frame(flac, (drflac_uint64)target)) {
				outr_discard_queued(&router);
				c = (uint64_t)target;
				ts.cur = (double)c / flac->sampleRate;
				ts.note = k == KEY_RIGHT ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			outr_abort(&router);
			break;
		}

		c += n;
		ts.cur = (double)c / flac->sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drflac_close(flac);
	free_crosstalk_cancellation(&xtc);
}

#endif /* USE_FOXEN_FLAC */

void play_dsf(char *name, int format, int flag)
{
	FILE *f = fopen(name, "rb");
	if (!f) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	DSDDecoder *decoder = dsd_decoder_init_file(f);
	if (!decoder) {
		printf("Error: failed to initialize DSD decoder for `%s`\n", name);
		fclose(f);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, decoder->sample_rate_pcm,
	              decoder->channels, format,
	              (flag & USE_DSD_ENCODE) ? 1 : 0,
	              (flag & USE_SUPER_RES) ? 1 : 0)) {
		dsd_decoder_free(decoder);
		fclose(f);
		return;
	}

	size_t sample_bytes = format == SND_PCM_FORMAT_FLOAT_LE
		? sizeof(float) : sizeof(int16_t);
	void *pcm_buf = malloc((size_t)FRAMES *
	                       (size_t)decoder->channels * sample_bytes);
	if (!pcm_buf) {
		outr_close(&router);
		dsd_decoder_free(decoder);
		fclose(f);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, decoder->sample_rate_pcm,
	                            decoder->channels);

	const char *base_codec =
		(decoder->file_type == DSD_FILE_DFF) ? "DFF" : "DSF";
	char codecbuf[32];
	snprintf(codecbuf, sizeof(codecbuf), "%s", base_codec);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = codecbuf;
	ts.rate = decoder->sample_rate_pcm;
	ts.bits = format == SND_PCM_FORMAT_FLOAT_LE ? 32 : 16;
	ts.channels = decoder->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = decoder->totalPCMFrameCount > 0 ?
	           (double)decoder->totalPCMFrameCount /
	           decoder->sample_rate_pcm : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t frames_played = 0;
	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		free_crosstalk_cancellation(&xtc);
		free(pcm_buf);
		outr_close(&router);
		dsd_decoder_free(decoder);
		fclose(f);
		tui_close();
		return;
	}

	if (router.use_dsd)
		snprintf(codecbuf, sizeof(codecbuf), "%s->DSD(%s)",
		         base_codec,
		         router.dsd.monitor_mode ? "mon" : "DoP");
	ts.rate = outr_output_rate(&router);
	tui_render(&ts);

	size_t n;
	while ((n = dsd_decoder_read_pcm_frames(
		        decoder, FRAMES, pcm_buf, format)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(
				&xtc, pcm_buf, n, decoder->channels, format);
		}

		if (format == SND_PCM_FORMAT_FLOAT_LE)
			outr_feed_float(&router, (const float *)pcm_buf, n);
		else
			outr_feed_s16(&router, (const int16_t *)pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES;
			g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res
				? "Super resolution on"
				: "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE;
			g_flag_diff ^= USE_DSD_ENCODE;
			if (router.use_dsd)
				snprintf(codecbuf, sizeof(codecbuf),
				         "%s->DSD(%s)", base_codec,
				         router.dsd.monitor_mode ? "mon" : "DoP");
			else
				snprintf(codecbuf, sizeof(codecbuf), "%s",
				         base_codec);
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = (decoder->file_type == DSD_FILE_DFF)
				? "seek not supported for DFF"
				: "seek not supported for DSF";
			tui_render(&ts);
		} else if (k) {
			outr_abort(&router);
			break;
		}

		frames_played += n;
		ts.cur = (double)frames_played /
		         decoder->sample_rate_pcm;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		ts.note = (n != FRAMES)
			? "! short read (end of file)" : NULL;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	free_crosstalk_cancellation(&xtc);
	free(pcm_buf);
	outr_close(&router);
	dsd_decoder_free(decoder);
	fclose(f);
}

#ifdef DR_MP3_IMPLEMENTATION
int play_mp3(char *name, int format, int flag)
{
	drmp3 mp3;
	if (!drmp3_init_file(&mp3, name, NULL)) {
		printf("Error: not a valid MP3 audio file!\n");
		return 1;
	}

	OutRouter router;
	if (outr_init(&router, dev, mp3.sampleRate, mp3.channels, format,
	              (flag & USE_DSD_ENCODE) ? 1 : 0,
	              (flag & USE_SUPER_RES) ? 1 : 0)) {
		drmp3_uninit(&mp3);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, mp3.sampleRate, mp3.channels);

	const int float_output = format == SND_PCM_FORMAT_FLOAT_LE;
	size_t sample_bytes = float_output ? sizeof(float) : sizeof(int16_t);
	void *pcm_buf = malloc((size_t)FRAMES * mp3.channels * sample_bytes);
	drmp3_uint64 totalPCMFrameCount;
	if (!pcm_buf) {
		outr_close(&router);
		drmp3_uninit(&mp3);
		free_crosstalk_cancellation(&xtc);
		return 1;
	}
	totalPCMFrameCount = drmp3_get_pcm_frame_count(&mp3);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd
		? (router.dsd.monitor_mode ? "MP3->DSD(mon)" : "MP3->DSD(DoP)")
		: "MP3";
	ts.rate = outr_output_rate(&router);
	ts.bits = float_output ? 32 : 16;
	ts.channels = mp3.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = totalPCMFrameCount > 0
		? (double)totalPCMFrameCount / mp3.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		free(pcm_buf);
		outr_close(&router);
		drmp3_uninit(&mp3);
		free_crosstalk_cancellation(&xtc);
		tui_close();
		return 1;
	}

	for (;;) {
		size_t n = float_output
			? drmp3_read_pcm_frames_f32(&mp3, FRAMES, (float *)pcm_buf)
			: drmp3_read_pcm_frames_s16(&mp3, FRAMES, (int16_t *)pcm_buf);
		if (n == 0) break;

		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(
				&xtc, pcm_buf, (int)n, mp3.channels, format);

		if (float_output)
			outr_feed_float(&router, (const float *)pcm_buf, n);
		else
			outr_feed_s16(&router, (const int16_t *)pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES;
			g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res
				? "Super resolution on" : "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE;
			g_flag_diff ^= USE_DSD_ENCODE;
			ts.codec = router.use_dsd
				? (router.dsd.monitor_mode
				   ? "MP3->DSD(mon)" : "MP3->DSD(DoP)")
				: "MP3";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * mp3.sampleRate *
			                (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (totalPCMFrameCount > 0 &&
			    (uint64_t)target > totalPCMFrameCount)
				target = (int64_t)totalPCMFrameCount;
			if (drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64)target)) {
				outr_discard_queued(&router);
				c = (uint64_t)target;
				ts.cur = (double)c / mp3.sampleRate;
				ts.note = k == KEY_RIGHT ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			outr_abort(&router);
			break;
		}

		c += n;
		ts.cur = (double)c / mp3.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drmp3_uninit(&mp3);
	free_crosstalk_cancellation(&xtc);
	return 0;
}

#else
int play_mp3(char *name, int format, int flag)
{
	short sample_buf[MP3_MAX_SAMPLES_PER_FRAME];
	int len;
	void *file_data = preload(name, &len);
	unsigned char *stream_pos;
	int bytes_left;
	if (!file_data || file_data == MAP_FAILED || len <= 100) {
		fprintf(stderr, "Error: cannot preload MP3 file\n");
		return 1;
	}
	stream_pos = (unsigned char *)file_data;
	bytes_left = len - 100;

	mp3_info_t info;
	mp3_decoder_t mp3 = mp3_create();
	int frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, &info);
	if (!frame_size) {
		printf("Error: not a valid MP3 audio file!\n");
		munmap(file_data, len);
		return 1;
	}

	printf("%dHz %dch\n", info.sample_rate, info.channels);
	OutRouter router;
	if (outr_init(&router, dev, info.sample_rate, info.channels, 0,
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		munmap(file_data, len);
		return 1;
	}
	if (router.use_dsd) {
		// このビルド(minimp3フォールバック)にはTUIが無いため、リアルタイムの
		// 'e'キー切替には未対応。-eの指定どおりDSD(DoP)出力に固定で再生する。
		printf("DSD encode (DoP) output enabled for this track\n");
	}
	if (outr_wait_open(&router, NULL) < 0) {
		outr_close(&router);
		munmap(file_data, len);
		return 1;
	}

	CrosstalkCancel xtc;
	if (flag & USE_CROSSTALK) {
		init_crosstalk_cancellation(&xtc, info.sample_rate, info.channels);
		printf(" with Crosstalk Cancellation\n");
	}

	int c = 0;
	uint64_t last_progress_ns = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0) &&
	       !key(outr_audio(&router), NULL)) {
		uint64_t now = aplay_monotonic_ns();
		if (last_progress_ns == 0 || now == 0 ||
		    now - last_progress_ns >= UI_REFRESH_INTERVAL_NS) {
			printf("\r%d", c);
			last_progress_ns = now;
		}

		int n_frames = info.audio_bytes / 2 / info.channels;

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, sample_buf, n_frames, info.channels, 0);
		}
		stream_pos += frame_size;
		bytes_left -= frame_size;

		outr_feed_s16(&router, sample_buf, (size_t)n_frames);

		c += frame_size;
		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h\n");

	outr_close(&router);
	mp3_free(mp3);
	munmap(file_data, len);
	if (flag & USE_CROSSTALK) {
		free_crosstalk_cancellation(&xtc);
	}
	return 0;
}
#endif

#define OGG_DECODE_FRAMES 4096

void play_ogg(char *name, int flag)
{
	int n;
	int error;
	short *outputs = NULL;
	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);

	if (!v) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}
	if (v->channels <= 0) {
		fprintf(stderr, "OGG: invalid channel count\n");
		stb_vorbis_close(v);
		return;
	}

	outputs = (short *)malloc(
		(size_t)OGG_DECODE_FRAMES *
		(size_t)v->channels * sizeof(short));
	if (!outputs) {
		stb_vorbis_close(v);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, v->sample_rate, v->channels, 0,
	              (flag & USE_DSD_ENCODE) != 0,
	              (flag & USE_SUPER_RES) != 0)) {
		free(outputs);
		stb_vorbis_close(v);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(
		&xtc, v->sample_rate, v->channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? "OGG->DSD(DoP)" : "OGG";
	ts.rate = outr_output_rate(&router);
	ts.channels = v->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		outr_close(&router);
		stb_vorbis_close(v);
		free_crosstalk_cancellation(&xtc);
		free(outputs);
		tui_close();
		return;
	}
	ts.codec = router.use_dsd
		? (router.dsd.monitor_mode
		   ? "OGG->DSD(mon)" : "OGG->DSD(DoP)")
		: "OGG";

	while ((n = stb_vorbis_get_frame_short_interleaved(
		        v, v->channels, outputs,
		        OGG_DECODE_FRAMES * v->channels)) > 0) {
		int k;

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(
				&xtc, outputs, n, v->channels, 0);
		}

		outr_feed_s16(&router, outputs, (size_t)n);

		k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES;
			g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res
				? "Super resolution on"
				: "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE;
			g_flag_diff ^= USE_DSD_ENCODE;
			ts.codec = router.use_dsd
				? (router.dsd.monitor_mode
				   ? "OGG->DSD(mon)"
				   : "OGG->DSD(DoP)")
				: "OGG";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta =
				(int64_t)SEEK_SECONDS * v->sample_rate *
				(k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (stb_vorbis_seek(v, (unsigned int)target)) {
				outr_discard_queued(&router);
				c = (uint64_t)target;
				ts.cur = (double)c / v->sample_rate;
				ts.note = k == KEY_RIGHT
					? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			outr_abort(&router);
			break;
		}

		c += (uint64_t)n;
		ts.cur = (double)c / v->sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) != 0;
		ts.xtc_atten = xtc_attenuation;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	outr_close(&router);
	stb_vorbis_close(v);
	free_crosstalk_cancellation(&xtc);
	free(outputs);
}

#define MAX_WMA_FRAME_LEN 4096
#define MAX_WMA_SUBFRAMES 16
#define MAX_PCM_BUFFER_SIZE (MAX_WMA_SUBFRAMES * MAX_WMA_FRAME_LEN * 8 * sizeof(short))
int play_wma(char *name, int flag)
{
	void *file_data = NULL;
	unsigned char *stream_pos;
	short *sample_buf = NULL;
	int bytes_left;

	int fd = open(name, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }

	int len = lseek(fd, 0, SEEK_END);
	if (len <= 0) {
		fprintf(stderr, "Error: invalid file size\n");
		close(fd);
		return 1;
	}

	lseek(fd, 0, SEEK_SET);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (file_data == MAP_FAILED) { perror("mmap"); return 1; }

	stream_pos = (unsigned char *)file_data;
	bytes_left = len;

	CodecContext cc = {0};
	cc.priv_data = calloc(1, sizeof(WMADecodeContext));
	if (!cc.priv_data) {
		fprintf(stderr, "Error: failed to allocate WMA context\n");
		munmap(file_data, len);
		return 1;
	}

	if (parse_wma_header(stream_pos, bytes_left, &cc) != 0) {
		fprintf(stderr, "Error: failed to parse WMA header\n");
		munmap(file_data, len); free(cc.priv_data); return 1;
	}

	WMADecodeContext *s = (WMADecodeContext *)cc.priv_data;
	if (wma_decode_init_fixed(&cc) < 0) {
		fprintf(stderr, "Error: failed to initialize WMA decoder\n");
		munmap(file_data, len); free(cc.priv_data); return 1;
	}

	size_t pcm_buffer_size = s->nb_channels * s->frame_len * MAX_WMA_SUBFRAMES * sizeof(short);
	if (pcm_buffer_size == 0 || pcm_buffer_size > MAX_PCM_BUFFER_SIZE || s->nb_channels == 0 || s->frame_len == 0) {
		fprintf(stderr, "Error: invalid PCM buffer parameters: nb_channels=%d, frame_len=%d, pcm_buffer_size=%zu\n",
		        s->nb_channels, s->frame_len, pcm_buffer_size);
		wma_decode_end(&cc); munmap(file_data, len); free(cc.priv_data); return 1;
	}

	sample_buf = malloc(pcm_buffer_size);
	if (!sample_buf) {
		fprintf(stderr, "Error: failed to allocate PCM buffer of size %zu bytes\n", pcm_buffer_size);
		wma_decode_end(&cc); munmap(file_data, len); free(cc.priv_data); return 1;
	}

	OutRouter router;
	if (outr_init(&router, dev, cc.sample_rate, cc.channels, 0,
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		fprintf(stderr, "Error: failed to initialize audio\n");
		wma_decode_end(&cc); munmap(file_data, len);
		free(sample_buf); free(cc.priv_data); return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, cc.sample_rate, cc.channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WMA->DSD(mon)" : "WMA->DSD(DoP)") : "WMA";
	ts.rate = outr_output_rate(&router);
	ts.channels = cc.channels;
	ts.device = dev;
	ts.use_time = 0;
	ts.unit = "bytes";
	ts.total_raw = (uint64_t)len;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t last_render_ns = 0;
	tui_open();
	if (outr_wait_open(&router, &ts) < 0) {
		outr_close(&router);
		wma_decode_end(&cc); munmap(file_data, len);
		free(sample_buf); free(cc.priv_data);
		free_crosstalk_cancellation(&xtc);
		tui_close();
		return 1;
	}
	while (bytes_left > 0) {
		int out_size = 0;
		int frame_size = wma_decode_superframe(&cc, sample_buf, &out_size, stream_pos, bytes_left);
		if (frame_size <= 0 || out_size <= 0) {
			fprintf(stderr, "Error: failed to decode WMA frame, frame_size=%d, out_size=%d, bytes_left=%d\n",
			        frame_size, out_size, bytes_left);
			break;
		}

		if (out_size > (int)pcm_buffer_size) {
			fprintf(stderr, "Error: decoded output size %d exceeds buffer size %zu\n", out_size, pcm_buffer_size);
			break;
		}

		if (cc.channels <= 0) {
			fprintf(stderr, "Error: invalid channel count %d\n",
			        cc.channels);
			break;
		}
		int frames = out_size / (sizeof(short) * cc.channels);
		if (frames <= 0 || frames > MAX_WMA_FRAME_LEN) {
			fprintf(stderr, "Error: invalid frame count %d, channels=%d\n", frames, cc.channels);
			break;
		}

		if (flag & USE_CROSSTALK) {
			if (!sample_buf) { fprintf(stderr, "Error: sample_buf is NULL\n"); break; }
			apply_crosstalk_cancellation(&xtc, sample_buf, frames, cc.channels, 0);
		}

		outr_feed_s16(&router, sample_buf, (size_t)frames);

		stream_pos += frame_size;
		bytes_left -= frame_size;

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == 's') {
			outr_toggle_super_res(&router);
			flag ^= USE_SUPER_RES; g_flag_diff ^= USE_SUPER_RES;
			ts.note = router.use_super_res ? "Super resolution on" : "Super resolution off";
			tui_render(&ts);
		} else if (k == 'e') {
			outr_toggle(&router);
			flag ^= USE_DSD_ENCODE; g_flag_diff ^= USE_DSD_ENCODE;
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WMA->DSD(mon)" : "WMA->DSD(DoP)") : "WMA";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = "seek not supported for WMA";
			tui_render(&ts);
		} else if (k) {
			outr_abort(&router);
			break;
		}

		ts.cur_raw = (uint64_t)(len - bytes_left);
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		aplay_render_throttled(&ts, &last_render_ns);
	}
	tui_close();

	outr_close(&router);
	wma_decode_end(&cc);
	munmap(file_data, len);
	free(sample_buf);
	free(cc.priv_data);
	free_crosstalk_cancellation(&xtc);
	return 0;
}

int play_aac(char *name, int flag)
{
    unsigned char *file_data;
    unsigned char *stream_pos;
    short sample_buf[AAC_BUF_SIZE * 2];
    int bytes_left;
    int resync_attempts = 0;
    (void)resync_attempts;

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        printf("Error: cannot open `%s`\n", name);
        return 1;
    }

    int samplerate, channels, profile;
    file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels, &profile);
    if (!file_data) {
        printf("Error: cannot read AAC data\n");
        close(fd);
        return 1;
    }
    stream_pos = file_data;

    AACFrameInfo info;
    memset(&info, 0, sizeof(AACFrameInfo));
    info.nChans = channels;
    info.sampRateCore = samplerate;
    info.profile = profile;

    HAACDecoder aac = AACInitDecoder();
    if (!aac) {
        fprintf(stderr, "Error: failed to initialize AAC decoder\n");
        free(file_data);
        close(fd);
        return 1;
    }
    AACSetRawBlockParams(aac, 0, &info);

    int output_samplerate = samplerate;
    int sbr_enabled = (profile == 5);
    if (sbr_enabled) {
        output_samplerate *= 2;
    } else if (samplerate <= 24000) {
        sbr_enabled = 1;
        info.profile = 5;
        output_samplerate *= 2;
        printf("Assuming potential HE-AAC with SBR, output samplerate: %dHz\n", output_samplerate);
    }
    OutRouter router;
    if (outr_init(&router, dev, output_samplerate, info.nChans, 0,
                  (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
        printf("Error: failed to initialize ALSA with %dHz, %dch\n", output_samplerate, info.nChans);
        free(file_data); close(fd); AACFreeDecoder(aac); return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

    tui_state_t ts = {0};
    ts.track_index = track_index;
    ts.track_total = track_total;
    ts.filename = get_basename(name);
    char dirbuf[PATH_MAX];
    get_dirpart(name, dirbuf, sizeof(dirbuf));
    ts.dir = dirbuf;
    ts.codec = router.use_dsd
        ? (router.dsd.monitor_mode
           ? (sbr_enabled ? "AAC/SBR->DSD(mon)" : "AAC->DSD(mon)")
           : (sbr_enabled ? "AAC/SBR->DSD(DoP)" : "AAC->DSD(DoP)"))
        : (sbr_enabled ? "AAC/SBR" : "AAC");
    ts.rate = outr_output_rate(&router);
    ts.channels = info.nChans;
    ts.device = dev;
    ts.use_time = 0;
    ts.unit = "bytes";
    ts.total_raw = (uint64_t)bytes_left;
    ts.volume = volume;
    ts.loop_mode = loop_mode;
    ts.format_filter = fmt_filter;

    uint64_t last_render_ns = 0;
    tui_open();
    if (outr_wait_open(&router, &ts) < 0) {
        outr_close(&router);
        free_crosstalk_cancellation(&xtc);
        free(file_data); close(fd); AACFreeDecoder(aac);
        tui_close();
        return 1;
    }
    while (bytes_left > 0) {
        int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
        if (verbose) {
            printf("\rDecoded %d bytes, %d bytes left, result=%d\n", (int)(stream_pos - file_data), bytes_left, r);
        }
        if (!r) {
            AACGetLastFrameInfo(aac, &info);
            int samples_per_frame = sbr_enabled ? 2048 : 1024;
            if (verbose) {
                printf("Frame: %d samples/channel, %d channels, %dHz\n", samples_per_frame, info.nChans, info.sampRateCore);
            }
            int frames = samples_per_frame;
            if (flag & USE_CROSSTALK) {
                apply_crosstalk_cancellation(&xtc, sample_buf, frames, info.nChans, 0);
            }
            outr_feed_s16(&router, sample_buf, (size_t)frames);
        } else {
            ts.note = "AAC decode error, stopping";
            tui_render(&ts);
            break;
        }

        int k = key(outr_audio(&router), &ts);
        if (k == 'c') {
            flag ^= USE_CROSSTALK;
            g_flag_diff ^= USE_CROSSTALK;
        } else if (k == 's') {
            outr_toggle_super_res(&router);
            flag ^= USE_SUPER_RES; g_flag_diff ^= USE_SUPER_RES;
            ts.note = router.use_super_res ? "Super resolution on" : "Super resolution off";
            tui_render(&ts);
        } else if (k == 'e') {
            outr_toggle(&router);
            flag ^= USE_DSD_ENCODE; g_flag_diff ^= USE_DSD_ENCODE;
            ts.codec = router.use_dsd
                ? (router.dsd.monitor_mode
                   ? (sbr_enabled ? "AAC/SBR->DSD(mon)" : "AAC->DSD(mon)")
                   : (sbr_enabled ? "AAC/SBR->DSD(DoP)" : "AAC->DSD(DoP)"))
                : (sbr_enabled ? "AAC/SBR" : "AAC");
            ts.rate = outr_output_rate(&router);
            ts.note = outr_status_note(&router);
            tui_render(&ts);
        } else if (k == KEY_LEFT || k == KEY_RIGHT) {
            ts.note = "seek not supported for AAC";
            tui_render(&ts);
        } else if (k) {
            outr_abort(&router);
            break;
        }

        ts.cur_raw = (uint64_t)(stream_pos - file_data);
        ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
        ts.xtc_atten = xtc_attenuation;
        aplay_render_throttled(&ts, &last_render_ns);
    }
    tui_close();

    outr_close(&router);
    AACFreeDecoder(aac);
    free(file_data);
    close(fd);
    free_crosstalk_cancellation(&xtc);
    return 0;
}

/* Optional GUI hooks for live playlist edits (aplay+ui.c). */
#ifndef APLAY_PLAYLIST_RELOAD_HOOK
#define APLAY_PLAYLIST_RELOAD_HOOK() 0
#endif
#ifndef APLAY_PLAYLIST_REQUESTED_TRACK
#define APLAY_PLAYLIST_REQUESTED_TRACK() 0
#endif

void play_dir(char *name, char *type, char *regexp, int flag)
{
	char path[1024];
	char resume_after[PATH_MAX];
	Reprog *compiled_regexp = NULL;
	int num, back = 0;

	if (regexp) {
		const char *error;
		compiled_regexp = regcomp(regexp, REG_ICASE, &error);
		if (!compiled_regexp) {
			fprintf(stderr, "regcomp: %s\n", error);
			return;
		}
	}

	resume_after[0] = '\0';
	path[0] = '\0';

	do {
		LS_LIST *ls = ls_dir(name, flag, &num);
		if (!ls) break;
		int start = 0;
		if (resume_after[0]) {
			for (int j = 0; j < num; j++) {
				if (!strcmp(ls[j].d_name, resume_after)) { start = j + 1; break; }
			}
			resume_after[0] = '\0';
		}
		int stop = 0;
		for (int i = start; i < num; i++) {
reload_check:
			if (APLAY_PLAYLIST_RELOAD_HOOK()) {
				char stay[PATH_MAX];
				snprintf(stay, sizeof(stay), "%s",
					(i >= 0 && i < num && ls[i].d_name[0]) ? ls[i].d_name : path);
				free(ls);
				ls = ls_dir(name, flag, &num);
				if (!ls) { num = 0; stop = 1; break; }
				int found = -1;
				for (int j = 0; j < num; j++) {
					if (stay[0] && !strcmp(ls[j].d_name, stay)) { found = j; break; }
				}
				int req = APLAY_PLAYLIST_REQUESTED_TRACK();
				if (req > 0 && req <= num) i = req - 1;
				else if (found >= 0) i = found;
				else if (i >= num) i = num > 0 ? num - 1 : 0;
				back = i - 1;
				track_total = num;
			}

			char *e = findExt(ls[i].d_name);
			if (type && !strstr(e, type)) continue;
			if (fmt_filter && !strstr(e, fmt_filter)) continue;
			if (compiled_regexp) {
				Resub m;
				if (regexec(compiled_regexp,
				            ls[i].d_name, &m, 0)) continue;
			}

			track_index = i + 1;
			track_total = num;
			snprintf(path, sizeof(path), "%s", ls[i].d_name);

			struct stat file_stat;
			if (stat(ls[i].d_name, &file_stat) < 0) { perror("stat"); continue; }
			if (file_stat.st_size == 0) { printf("File size is 0: %s\n", ls[i].d_name); continue; }

			// Fold in any 'c'/'e'/'s' toggles made during previous tracks so the
			// crosstalk/DSD/super-resolution state carries over between tracks.
			int cur_flag = flag ^ g_flag_diff;
			int cur_format = cur_flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;
			cmd = 0;

			if (strstr(e, "flac")) {
				play_flac(path, cur_format, cur_flag);
			} else if (strstr(e, "mp3")) {
				play_mp3(path, cur_format, cur_flag);
			} else if (strstr(e, "mp4")) {
				play_aac(path, cur_flag);
			} else if (strstr(e, "m4a")) {
				play_aac(path, cur_flag);
			} else if (strstr(e, "ogg")) {
				play_ogg(path, cur_flag);
			} else if (strstr(e, "wav")) {
				play_wav(path, cur_format, cur_flag);
			} else if (strstr(e, "wma")) {
				play_wma(path, cur_flag);
			} else if (strstr(e, "dsf") || strstr(e, "dff")) {
				play_dsf(path, cur_format, cur_flag);
			} else {
				continue;
			}

			if (cmd == '\\' || cmd == 'p' || cmd == 'b') {
				i = back;
			} else if (cmd == 'd') {
				char cur_dir[PATH_MAX];
				get_dirpart(ls[i].d_name, cur_dir, sizeof(cur_dir));
				while (i + 1 < num) {
					char next_dir[PATH_MAX];
					get_dirpart(ls[i + 1].d_name, next_dir, sizeof(next_dir));
					if (strcmp(cur_dir, next_dir) != 0) break;
					i++;
				}
				back = i - 1;
			}
			if (cmd == 'q' || cmd == 0x1b) { stop = 1; break; }
			/* 'A' = abort current track and rebuild playlist (Add folder…). */
			if (cmd == 'A') {
				cmd = 0;
				goto reload_check;
			}
			if (cmd != 'd') back = i - 1;
		}
		int again = APLAY_PLAYLIST_RELOAD_HOOK();
		free(ls);
		if (stop) break;
		if (again) {
			snprintf(resume_after, sizeof(resume_after), "%s", path);
			continue;
		}
	} while (loop_mode);

	free(compiled_regexp);
}

#include <sched.h>
void set_realtime_priority(void)
{
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
		perror("Failed to set real-time priority");
	}
}
void set_cpu(char *c)
{
	char buff[256];
	long cpu_count;

	if (!c || !c[0]) return;
	cpu_count = sysconf(_SC_NPROCESSORS_CONF);
	if (cpu_count < 1) cpu_count = 1;

	for (long i = 0; i < cpu_count; ++i) {
		FILE *fp;
		snprintf(buff, sizeof(buff),
		         "/sys/devices/system/cpu/cpu%ld/cpufreq/"
		         "scaling_governor", i);
		fp = fopen(buff, "w");
		if (!fp) continue;
		(void)fprintf(fp, "%s", c);
		fclose(fp);
	}
}

void aplay_copy_dev(const char *name)
{
	if (!name || !name[0]) return;
	snprintf(g_dev_storage, sizeof(g_dev_storage), "%s", name);
	dev = g_dev_storage;
}

/* Non-blocking open probe: 1 if the PCM looks like a usable playback target.
 * -EBUSY counts as success (device exists; another process holds it). */
static int aplay_device_can_open(const char *name)
{
	if (!name || !name[0]) return 0;
	snd_pcm_t *pcm = NULL;
	int rc = snd_pcm_open(&pcm, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (rc >= 0) {
		snd_pcm_close(pcm);
		return 1;
	}
	return rc == -EBUSY;
}

int aplay_auto_select_device(void)
{
	aplay_refresh_devices();
	int first_enumerated = -1;
	for (int i = 0; i < g_device_count; i++) {
		/* Skip the placeholder entry aplay_refresh_devices() adds when
		 * no cards are present (it just echoes the unset default). */
		if (g_devices[i].card < 0) continue;
		if (first_enumerated < 0) first_enumerated = i;
		const char *name = g_devices[i].name;
		if (!aplay_device_can_open(name)) continue;
		aplay_copy_dev(name);
		printf("aplay+: auto-selected ALSA device %s\n",
		       g_devices[i].label[0] ? g_devices[i].label : name);
		return 0;
	}
	/* Enumeration found a playback PCM but open probe failed for every
	 * entry (permissions, etc.) — still prefer the first real hw:N,M
	 * over the hardcoded hw:0,0 placeholder. */
	if (first_enumerated >= 0) {
		const char *name = g_devices[first_enumerated].name;
		aplay_copy_dev(name);
		printf("aplay+: auto-selected ALSA device %s\n",
		       g_devices[first_enumerated].label[0]
		           ? g_devices[first_enumerated].label : name);
		return 0;
	}
	/* Last resorts when card enumeration found nothing usable. */
	static const char *fallbacks[] = { "default", "plughw:0,0", NULL };
	for (int i = 0; fallbacks[i]; i++) {
		if (!aplay_device_can_open(fallbacks[i])) continue;
		aplay_copy_dev(fallbacks[i]);
		printf("aplay+: auto-selected ALSA device %s\n", fallbacks[i]);
		return 0;
	}
	fprintf(stderr, "aplay+: no usable ALSA playback device found (try -d hw:N,M)\n");
	return -1;
}

static int aplay_device_has_playback(snd_ctl_t *ctl, int device)
{
	snd_pcm_info_t *pcminfo;
	snd_pcm_info_alloca(&pcminfo);
	snd_pcm_info_set_device(pcminfo, device);
	snd_pcm_info_set_subdevice(pcminfo, 0);
	snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);
	return snd_ctl_pcm_info(ctl, pcminfo) >= 0;
}

void aplay_refresh_devices(void)
{
	g_device_count = 0;
	snd_ctl_t *ctl;
	snd_ctl_card_info_t *info;
	snd_ctl_card_info_alloca(&info);
	int card = -1;
	while (g_device_count < APLAY_MAX_DEVICES &&
	       snd_card_next(&card) >= 0 && card >= 0) {
		char ctl_name[32];
		snprintf(ctl_name, sizeof(ctl_name), "hw:%d", card);
		if (snd_ctl_open(&ctl, ctl_name, 0) < 0) continue;
		snd_ctl_card_info(ctl, info);
		const char *card_name = snd_ctl_card_info_get_name(info);
		int device = -1;
		while (g_device_count < APLAY_MAX_DEVICES &&
		       snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0) {
			if (!aplay_device_has_playback(ctl, device)) continue;
			AplayDevice *d = &g_devices[g_device_count];
			memset(d, 0, sizeof(*d));
			d->card = card;
			d->pcm = device;
			snprintf(d->name, sizeof(d->name), "hw:%d,%d", card, device);
			if (card_name && card_name[0]) {
				snprintf(d->card_name, sizeof(d->card_name), "%s", card_name);
				snprintf(d->label, sizeof(d->label), "hw:%d,%d (%s)", card, device, card_name);
			} else {
				snprintf(d->card_name, sizeof(d->card_name), "card %d", card);
				snprintf(d->label, sizeof(d->label), "%s", d->name);
			}
			g_device_count++;
		}
		snd_ctl_close(ctl);
	}
	if (g_device_count == 0) {
		memset(&g_devices[0], 0, sizeof(g_devices[0]));
		snprintf(g_devices[0].name, sizeof(g_devices[0].name), "%s", g_dev_storage);
		snprintf(g_devices[0].label, sizeof(g_devices[0].label), "%s", g_dev_storage);
		snprintf(g_devices[0].card_name, sizeof(g_devices[0].card_name), "%s", g_dev_storage);
		g_devices[0].card = -1;
		g_devices[0].pcm = -1;
		g_device_count = 1;
	}
}

int aplay_device_count(void)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	return g_device_count;
}

const char *aplay_device_name(int idx)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	if (idx < 0 || idx >= g_device_count) return NULL;
	return g_devices[idx].name;
}

const char *aplay_device_label(int idx)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	if (idx < 0 || idx >= g_device_count) return NULL;
	return g_devices[idx].label;
}

int aplay_device_card(int idx)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	if (idx < 0 || idx >= g_device_count) return -1;
	return g_devices[idx].card;
}

int aplay_device_pcm(int idx)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	if (idx < 0 || idx >= g_device_count) return -1;
	return g_devices[idx].pcm;
}

const char *aplay_device_card_name(int idx)
{
	if (g_device_count <= 0) aplay_refresh_devices();
	if (idx < 0 || idx >= g_device_count) return NULL;
	return g_devices[idx].card_name;
}

int aplay_device_unique_cards(int *out_cards, int max_out)
{
	if (!out_cards || max_out <= 0) return 0;
	if (g_device_count <= 0) aplay_refresh_devices();
	int n = 0;
	for (int i = 0; i < g_device_count && n < max_out; i++) {
		int card = g_devices[i].card;
		int seen = 0;
		for (int j = 0; j < n; j++) {
			if (out_cards[j] == card) { seen = 1; break; }
		}
		if (!seen) out_cards[n++] = card;
	}
	return n;
}

static int aplay_find_device_index(const char *name)
{
	if (!name) return -1;
	if (g_device_count <= 0) aplay_refresh_devices();
	for (int i = 0; i < g_device_count; i++) {
		if (!strcmp(g_devices[i].name, name)) return i;
	}
	return -1;
}

int aplay_set_device(AUDIO *a, tui_state_t *ts, const char *name)
{
	if (!name || !name[0]) return -1;
	char prev[sizeof(g_dev_storage)];
	snprintf(prev, sizeof(prev), "%s", g_dev_storage);
	aplay_copy_dev(name);

	OutRouter *router = (OutRouter *)g_active_router_ptr;
	int ok = -1;
	if (router) {
		ok = outr_set_device(router, g_dev_storage);
	} else if (a) {
		AUDIO_release(a);
		snprintf(a->dev, sizeof(a->dev), "%s", g_dev_storage);
		ok = AUDIO_reopen(a);
		if (ok == 0) apply_alsa_volume();
		else ok = -1;
	} else {
		ok = 0; /* no active stream; global updated for next open */
	}

	if (ok != 0) {
		aplay_copy_dev(prev);
		if (router) {
			outr_set_device(router, g_dev_storage);
		} else if (a) {
			AUDIO_release(a);
			snprintf(a->dev, sizeof(a->dev), "%s", g_dev_storage);
			AUDIO_reopen(a);
			apply_alsa_volume();
		}
		if (ts) {
			ts->device = dev;
			ts->note = "Device switch failed";
			tui_render(ts);
		}
		return -1;
	}

	if (ts) {
		ts->device = dev;
		ts->note = "Device switched";
		if (router) {
			ts->rate = outr_output_rate(router);
			ts->note = outr_status_note(router);
			/* Prefer a clear device note over the PCM/DSD status string. */
			static char note_buf[160];
			snprintf(note_buf, sizeof(note_buf), "dev %s", g_dev_storage);
			ts->note = note_buf;
		} else {
			static char note_buf[160];
			snprintf(note_buf, sizeof(note_buf), "dev %s", g_dev_storage);
			ts->note = note_buf;
		}
		tui_render(ts);
	}
	return 0;
}

int aplay_select_device_index(AUDIO *a, tui_state_t *ts, int idx)
{
	const char *name = aplay_device_name(idx);
	if (!name) return -1;
	return aplay_set_device(a, ts, name);
}

int aplay_cycle_device(AUDIO *a, tui_state_t *ts)
{
	int n = aplay_device_count();
	if (n <= 0) return -1;
	int cur = aplay_find_device_index(g_dev_storage);
	int next = (cur < 0) ? 0 : (cur + 1) % n;
	return aplay_select_device_index(a, ts, next);
}

int aplay_handle_device_keys(AUDIO *a, tui_state_t *ts, int c)
{
	int requested = __atomic_exchange_n(&g_device_select_idx, -1, __ATOMIC_SEQ_CST);
	if (requested >= 0) {
		aplay_select_device_index(a, ts, requested);
		return 1; /* consumed */
	}
	if (c == 'D') {
		aplay_cycle_device(a, ts);
		return 1;
	}
	return 0;
}

void list_alsa_devices(void)
{
	aplay_refresh_devices();
	printf("Available ALSA playback devices:\n");
	for (int i = 0; i < g_device_count; i++)
		printf("  [%d] %s\n", i, g_devices[i].label);
	if (g_device_count == 0)
		printf("  (none found)\n");
}

#endif /* APLAY_ENGINE_IMPLEMENTATION */
