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

#include "alsa.h"
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
#define FRAMES               32
#define MAX_DELAY_SAMPLES    16
#define SEEK_SECONDS         10
#define VOLUME_STEP          0.05f
#define XTC_ATTEN_STEP       0.05f
#define KEY_POLL_INTERVAL_US 4000  // gate kbhit() to ≤250 calls/sec

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

#define DSD_ENC_OSR         64
#define DSD_ENC_INPUT_CHUNK FRAMES
#define DSD_ENC_DSD_BYTES_PER_CH (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8 + 2)
#define DSD_ENC_DOP_FRAMES  (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 16)

typedef struct {
	DSDEncoder *enc;
	AUDIO a;
	int channels;
	uint8_t *dsd_buf;
	int32_t *dop_buf;

	// Fallback for hardware that can't open DoP's high ALSA rate
	// (pcm_rate*OSR/16): feed the DSD bits we just made straight back through
	// dsd.h's decoder (the same filter/AGC/limiter pipeline used for real
	// DSD files) and play the result as ordinary PCM (PCM -> DSD -> PCM).
	int monitor_mode;
	AUDIO mon_a;
	DSDDecoder *mon_decoder;
	uint8_t *mon_block_buf;
	float *mon_pcm_buf;
	size_t mon_frames_per_block;

	// Writing to ALSA every single block leaves FIR interpolation + delta-sigma
	// + 4-stage IIR decode too little slack and underruns easily, so
	// MON_ACCUM_BLOCKS blocks are batched per write.
	float *mon_accum_buf;
	size_t mon_accum_fill;
	size_t mon_accum_period;
} DsdSink;

#define MON_ACCUM_BLOCKS 8
#define DSD_ENC_DSD_BYTES_PER_CH_EXACT (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8)

int dsdsink_open(DsdSink *sink, const char *dev, int pcm_rate, int channels)
{
	memset(sink, 0, sizeof(*sink));
	sink->channels = channels;

	sink->enc = dsd_encoder_init(channels, pcm_rate, pcm_rate * DSD_ENC_OSR);
	if (!sink->enc) {
		fprintf(stderr, "DSD encoder init failed (channels=%d, rate=%d)\n", channels, pcm_rate);
		return -1;
	}

	sink->dsd_buf = (uint8_t*)malloc(DSD_ENC_DSD_BYTES_PER_CH * channels);
	sink->dop_buf = (int32_t*)malloc((size_t)DSD_ENC_DOP_FRAMES * channels * sizeof(int32_t));
	if (!sink->dsd_buf || !sink->dop_buf) {
		goto fail_enc;
	}

	int dop_rate = pcm_rate * DSD_ENC_OSR / 16;
	if (AUDIO_init(&sink->a, (char*)dev, dop_rate, channels, DSD_ENC_DOP_FRAMES, 1, SND_PCM_FORMAT_S24_LE) == 0) {
		if (sink->a.freq == (unsigned int)dop_rate) {
			printf("DSD encode (DoP) output: %d Hz PCM -> DSD64 @ %d Hz DoP / S24_LE / %dch\n",
			       pcm_rate, dop_rate, channels);
			return 0;
		}
		fprintf(stderr, "DoP: rejected ALSA rate remap %d -> %u Hz\n", dop_rate, sink->a.freq);
		AUDIO_close(&sink->a);
		memset(&sink->a, 0, sizeof(sink->a));
	}

	fprintf(stderr, "DoP output unavailable at %d Hz, falling back to PCM->DSD->PCM monitor mode\n", dop_rate);

	int dsd_rate = pcm_rate * DSD_ENC_OSR;
	sink->mon_decoder = dsd_decoder_init_raw(channels, dsd_rate, DSD_ENC_DSD_BYTES_PER_CH_EXACT);
	if (!sink->mon_decoder) {
		fprintf(stderr, "Failed to init monitor-mode DSD decoder\n");
		goto fail_enc;
	}

	static const int mon_rate_prefs[] = {
		384000, 352800, 192000, 176400, 96000, 88200, 48000, 44100
	};
	int cands[1 + (int)(sizeof(mon_rate_prefs) / sizeof(mon_rate_prefs[0]))];
	int ncands = 0;
	for (size_t i = 0; i < sizeof(mon_rate_prefs) / sizeof(mon_rate_prefs[0]); ++i) {
		int r = mon_rate_prefs[i];
		if (r == pcm_rate || dsd_rate % r != 0) continue;
		int dec = dsd_rate / r;
		if (dec < 8 || dec > 512) continue;
		cands[ncands++] = r;
	}
	cands[ncands++] = pcm_rate;

	int mon_rate = 0;
	for (int i = 0; i < ncands && !mon_rate; ++i) {
		if (dsd_decoder_set_pcm_rate(sink->mon_decoder, cands[i]) != 0) continue;
		sink->mon_frames_per_block = dsd_decoder_frames_per_block(sink->mon_decoder);
		if (sink->mon_frames_per_block == 0) continue;
		sink->mon_accum_period = sink->mon_frames_per_block * MON_ACCUM_BLOCKS;
		if (AUDIO_init(&sink->mon_a, (char*)dev, (unsigned int)cands[i], channels,
		               (int)sink->mon_accum_period, 4, SND_PCM_FORMAT_FLOAT_LE) != 0) continue;
		if (sink->mon_a.freq != (unsigned int)cands[i]) {
			fprintf(stderr, "monitor: rejected ALSA rate remap %d -> %u Hz\n", cands[i], sink->mon_a.freq);
			AUDIO_close(&sink->mon_a);
			memset(&sink->mon_a, 0, sizeof(sink->mon_a));
			continue;
		}
		mon_rate = cands[i];
	}
	if (!mon_rate) {
		fprintf(stderr, "Failed to open ALSA device for monitor-mode PCM output (tried source %d Hz and fallbacks)\n", pcm_rate);
		dsd_decoder_free(sink->mon_decoder);
		goto fail_enc;
	}

	sink->mon_block_buf = (uint8_t*)malloc((size_t)DSD_ENC_DSD_BYTES_PER_CH_EXACT * channels);
	sink->mon_pcm_buf = (float*)malloc(sink->mon_frames_per_block * (size_t)channels * sizeof(float));
	sink->mon_accum_buf = (float*)malloc(sink->mon_accum_period * (size_t)channels * sizeof(float));
	sink->mon_accum_fill = 0;
	if (!sink->mon_block_buf || !sink->mon_pcm_buf || !sink->mon_accum_buf) {
		AUDIO_close(&sink->mon_a);
		dsd_decoder_free(sink->mon_decoder);
		free(sink->mon_block_buf);
		free(sink->mon_pcm_buf);
		free(sink->mon_accum_buf);
		goto fail_enc;
	}

	sink->monitor_mode = 1;
	printf("DSD encode (monitor) output: %d Hz PCM -> DSD64 -> %d Hz PCM via dsd.h (DoP hardware unavailable) / %dch\n",
	       pcm_rate, mon_rate, channels);
	return 0;

fail_enc:
	dsd_encoder_free(sink->enc);
	free(sink->dsd_buf);
	free(sink->dop_buf);
	return -1;
}

void dsdsink_write(DsdSink *sink, const float *pcm, size_t frames)
{
	size_t bytes_per_ch = 0;
	if (dsd_encoder_process_raw(sink->enc, pcm, frames, sink->dsd_buf,
	                             DSD_ENC_DSD_BYTES_PER_CH * sink->channels,
	                             &bytes_per_ch) != 0) {
		return;
	}

	const uint8_t *ch_ptrs[DSDENC_MAX_CHANNELS];
	for (int ch = 0; ch < sink->channels; ch++) {
		ch_ptrs[ch] = sink->dsd_buf + (size_t)ch * DSD_ENC_DSD_BYTES_PER_CH;
	}

	if (g_dsd_raw_file) {
		for (int ch = 0; ch < sink->channels; ch++) {
			fwrite(ch_ptrs[ch], 1, bytes_per_ch, g_dsd_raw_file);
		}
	}

	if (sink->monitor_mode) {
		for (int ch = 0; ch < sink->channels; ch++) {
			memcpy(sink->mon_block_buf + (size_t)ch * DSD_ENC_DSD_BYTES_PER_CH_EXACT,
			       ch_ptrs[ch], DSD_ENC_DSD_BYTES_PER_CH_EXACT);
		}
		dsd_decoder_feed_block(sink->mon_decoder, sink->mon_block_buf);
		size_t out_frames = dsd_decoder_read_pcm_frames(sink->mon_decoder,
		                                                 sink->mon_frames_per_block,
		                                                 sink->mon_pcm_buf,
		                                                 SND_PCM_FORMAT_FLOAT_LE);
		if (out_frames > 0) {
			memcpy(sink->mon_accum_buf + sink->mon_accum_fill * (size_t)sink->channels,
			       sink->mon_pcm_buf, out_frames * (size_t)sink->channels * sizeof(float));
			sink->mon_accum_fill += out_frames;
		}
		if (sink->mon_accum_fill >= sink->mon_accum_period) {
			memcpy(sink->mon_a.buffer, sink->mon_accum_buf, sink->mon_accum_period * (size_t)sink->channels * sizeof(float));
			AUDIO_play0(&sink->mon_a);
			AUDIO_wait(&sink->mon_a, 100);
			sink->mon_accum_fill = 0;
		}
		return;
	}

	size_t dop_frames = 0;
	size_t even_bytes = bytes_per_ch & ~((size_t)1);
	dsd_encoder_pack_dop(sink->enc, ch_ptrs, even_bytes, sink->dop_buf, &dop_frames);

	memcpy(sink->a.buffer, sink->dop_buf, dop_frames * sink->channels * sizeof(int32_t));
	AUDIO_play0(&sink->a);
	AUDIO_wait(&sink->a, 100);
}

void dsdsink_close(DsdSink *sink)
{
	if (sink->monitor_mode) {
		if (sink->mon_accum_fill > 0 && sink->mon_accum_buf) {
			size_t remain = sink->mon_accum_period - sink->mon_accum_fill;
			memset(sink->mon_accum_buf + sink->mon_accum_fill * (size_t)sink->channels,
			       0, remain * (size_t)sink->channels * sizeof(float));
			memcpy(sink->mon_a.buffer, sink->mon_accum_buf, sink->mon_accum_period * (size_t)sink->channels * sizeof(float));
			AUDIO_play0(&sink->mon_a);
			AUDIO_wait(&sink->mon_a, 100);
		}
		AUDIO_close(&sink->mon_a);
		dsd_decoder_free(sink->mon_decoder);
		free(sink->mon_block_buf);
		free(sink->mon_accum_buf);
	} else {
		AUDIO_close(&sink->a);
	}
	dsd_encoder_free(sink->enc);
	free(sink->dsd_buf);
	free(sink->dop_buf);
	free(sink->mon_pcm_buf);
}

// ============================================================
// DsdAccum: fixed-FRAMES staging buffer between variable-size decoders and
// the DSD encoder that needs exactly FRAMES per call.
// ============================================================
#define DSD_ACCUM_CAPACITY 8192

typedef struct {
	float *buf;
	size_t fill;
	size_t capacity;
	int channels;
} DsdAccum;

void dsdaccum_init(DsdAccum *ac, int channels, size_t capacity_frames)
{
	ac->channels = channels;
	ac->capacity = capacity_frames;
	ac->buf = (float*)malloc(capacity_frames * (size_t)channels * sizeof(float));
	ac->fill = 0;
}
void dsdaccum_free(DsdAccum *ac) { free(ac->buf); ac->buf = NULL; }

void dsdaccum_push_s16(DsdAccum *ac, const int16_t *src, size_t frames)
{
	if (ac->fill + frames > ac->capacity) frames = ac->capacity - ac->fill;
	size_t off = ac->fill * (size_t)ac->channels;
	size_t n = frames * (size_t)ac->channels;
	for (size_t i = 0; i < n; i++) ac->buf[off + i] = src[i] / 32768.0f;
	ac->fill += frames;
}
void dsdaccum_push_f32(DsdAccum *ac, const float *src, size_t frames)
{
	if (ac->fill + frames > ac->capacity) frames = ac->capacity - ac->fill;
	memcpy(ac->buf + ac->fill * (size_t)ac->channels, src, frames * (size_t)ac->channels * sizeof(float));
	ac->fill += frames;
}
void dsdaccum_drain(DsdAccum *ac, DsdSink *sink)
{
	size_t off = 0;
	while (ac->fill - off >= DSD_ENC_INPUT_CHUNK) {
		dsdsink_write(sink, ac->buf + off * (size_t)ac->channels, DSD_ENC_INPUT_CHUNK);
		off += DSD_ENC_INPUT_CHUNK;
	}
	size_t remain = ac->fill - off;
	if (remain > 0 && off > 0) {
		memmove(ac->buf, ac->buf + off * (size_t)ac->channels, remain * (size_t)ac->channels * sizeof(float));
	}
	ac->fill = remain;
}

// ============================================================
// OutRouter: routes decoded audio to either normal PCM or DSD(DoP) output,
// switchable live during playback with the 'e' key.
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
} OutRouter;

void outr_open_pcm(OutRouter *r)
{
	if (r->pcm_open) return;
	if (r->dsd_open) { dsdsink_close(&r->dsd); r->dsd_open = 0; }
	if (AUDIO_init(&r->pcm, r->dev_buf, r->sample_rate, r->channels, FRAMES, 1, r->format) == 0) {
		r->pcm_open = 1;
	}
}
void outr_open_dsd(OutRouter *r)
{
	if (r->dsd_open) return;
	if (r->pcm_open) { AUDIO_close(&r->pcm); r->pcm_open = 0; }
	if (dsdsink_open(&r->dsd, r->dev_buf, r->sample_rate, r->channels) == 0) {
		r->dsd_open = 1;
		r->accum.fill = 0;
	} else {
		fprintf(stderr, "DSD output unavailable (DoP and monitor mode both failed), falling back to normal PCM\n");
		r->use_dsd = 0;
		outr_open_pcm(r);
	}
}
int outr_init(OutRouter *r, const char *dev_name, int sample_rate, int channels, int format,
              int start_with_dsd, int start_with_super_res)
{
	memset(r, 0, sizeof(*r));
	r->sample_rate = sample_rate;
	r->channels = channels;
	r->format = format;
	snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", dev_name);
	dsdaccum_init(&r->accum, channels, DSD_ACCUM_CAPACITY);
	r->i16_scratch = (int16_t*)malloc(DSD_ACCUM_CAPACITY * (size_t)channels * sizeof(int16_t));
	r->s16_f32 = (float*)malloc(DSD_ACCUM_CAPACITY * (size_t)channels * sizeof(float));
	r->sr_scratch = (float*)malloc(DSD_ACCUM_CAPACITY * (size_t)channels * sizeof(float));
	r->sr_prev = (float*)calloc((size_t)channels, sizeof(float));
	r->use_dsd = start_with_dsd ? 1 : 0;
	r->use_super_res = start_with_super_res ? 1 : 0;
	if (r->use_dsd) outr_open_dsd(r); else outr_open_pcm(r);
	g_active_router_ptr = r;
	return (r->pcm_open || r->dsd_open) ? 0 : -1;
}
void outr_toggle(OutRouter *r)
{
	r->use_dsd = !r->use_dsd;
	if (r->use_dsd) outr_open_dsd(r); else outr_open_pcm(r);
}
void outr_toggle_super_res(OutRouter *r)
{
	r->use_super_res = !r->use_super_res;
	r->sr_have_prev = 0;
}
AUDIO *outr_audio(OutRouter *r)
{
	if (!r->use_dsd) return &r->pcm;
	return r->dsd.monitor_mode ? &r->dsd.mon_a : &r->dsd.a;
}
int outr_output_rate(OutRouter *r)
{
	if (!r->use_dsd) return r->sample_rate;
	if (r->dsd.monitor_mode) return (int)r->dsd.mon_a.freq;
	return (int)r->dsd.a.freq;
}
const char *outr_status_note(OutRouter *r)
{
	if (!r->use_dsd) return "PCM output";
	return r->dsd.monitor_mode ? "DSD monitor (PCM->DSD->PCM, no DoP hw)" : "DSD(DoP)output";
}
void outr_close(OutRouter *r)
{
	if (g_active_router_ptr == r) g_active_router_ptr = NULL;
	if (r->pcm_open) AUDIO_close(&r->pcm);
	if (r->dsd_open) dsdsink_close(&r->dsd);
	dsdaccum_free(&r->accum);
	free(r->i16_scratch);
	free(r->s16_f32);
	free(r->sr_scratch);
	free(r->sr_prev);
}

/* Live-switch the ALSA device while keeping the current PCM/DSD mode.
 * Returns 0 on success, -1 if the new device could not be opened (previous
 * device is restored when possible). */
int outr_set_device(OutRouter *r, const char *name)
{
	if (!r || !name || !name[0]) return -1;
	char prev[sizeof(r->dev_buf)];
	snprintf(prev, sizeof(prev), "%s", r->dev_buf);
	int was_dsd = r->use_dsd;
	snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", name);
	if (r->pcm_open) { AUDIO_close(&r->pcm); r->pcm_open = 0; }
	if (r->dsd_open) { dsdsink_close(&r->dsd); r->dsd_open = 0; }
	r->use_dsd = was_dsd;
	if (was_dsd) outr_open_dsd(r); else outr_open_pcm(r);
	if (!r->pcm_open && !r->dsd_open) {
		snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", prev);
		r->use_dsd = was_dsd;
		if (was_dsd) outr_open_dsd(r); else outr_open_pcm(r);
		return -1;
	}
	apply_alsa_volume();
	return 0;
}
void outr_feed_float(OutRouter *r, const float *pcm, size_t frames)
{
	/* wave-super-resolution's detail reconstruction at unity rate:
	 * y[n] = x[n] + x[n] - (x[n-1] + x[n+1]) / 2.  Keep one frame of
	 * history so block boundaries behave like a continuous stream. */
	if (r->use_super_res && frames > 0 && frames <= DSD_ACCUM_CAPACITY) {
		size_t samples = frames * (size_t)r->channels;
		memcpy(r->sr_scratch, pcm, samples * sizeof(float));
		for (size_t i = 0; i < frames; ++i) {
			for (int ch = 0; ch < r->channels; ++ch) {
				size_t p = i * (size_t)r->channels + (size_t)ch;
				float cur = pcm[p];
				float prev = (i > 0) ? pcm[p - r->channels]
				                         : (r->sr_have_prev ? r->sr_prev[ch] : cur);
				float next = (i + 1 < frames) ? pcm[p + r->channels] : cur;
				float v = cur + cur - 0.5f * (prev + next);
				r->sr_scratch[p] = fmaxf(-1.0f, fminf(1.0f, v));
			}
		}
		memcpy(r->sr_prev, pcm + (frames - 1) * (size_t)r->channels,
		       (size_t)r->channels * sizeof(float));
		r->sr_have_prev = 1;
		pcm = r->sr_scratch;
	}
	if (r->use_dsd) {
		dsdaccum_push_f32(&r->accum, pcm, frames);
		dsdaccum_drain(&r->accum, &r->dsd);
	} else {
		if (r->format == SND_PCM_FORMAT_FLOAT_LE) {
			AUDIO_play(&r->pcm, (char*)pcm, (int)frames);
		} else {
			size_t n = frames * (size_t)r->channels;
			for (size_t i = 0; i < n; i++) {
				float v = pcm[i] * 32767.0f;
				if (v > 32767.0f) v = 32767.0f;
				if (v < -32768.0f) v = -32768.0f;
				r->i16_scratch[i] = (int16_t)v;
			}
			AUDIO_play(&r->pcm, (char*)r->i16_scratch, (int)frames);
		}
		AUDIO_wait(&r->pcm, 100);
	}
}
void outr_feed_s16(OutRouter *r, const int16_t *pcm, size_t frames)
{
	if (frames > DSD_ACCUM_CAPACITY) frames = DSD_ACCUM_CAPACITY;
	size_t n = frames * (size_t)r->channels;
	for (size_t i = 0; i < n; ++i) r->s16_f32[i] = pcm[i] / 32768.0f;
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

void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
	if (channels != 2) return;
	// 音速343m/sを基準に、スピーカー間距離から遅延を計算
	xtc->delay_samples = (int)(sample_rate * (speaker_distance_m / 343.0));
	xtc->attenuation = xtc_attenuation;
	xtc->delay_buffer_size = xtc->delay_samples * 2;
	if (xtc->delay_buffer_size < 2) xtc->delay_buffer_size = 2;
	xtc->delay_buffer = (float*)calloc(xtc->delay_buffer_size, sizeof(float));
	xtc->delay_index = 0;
}

void free_crosstalk_cancellation(CrosstalkCancel *xtc)
{
	if (xtc->delay_buffer) {
		free(xtc->delay_buffer);
		xtc->delay_buffer = NULL;
	}
}

void apply_crosstalk_cancellation(CrosstalkCancel *xtc, void *buffer, int frames, int channels, int format)
{
	if (channels != 2) return;
	xtc->attenuation = xtc_attenuation;

	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		float *data = (float*)buffer;
		float temp[frames * 2];
		memcpy(temp, data, frames * 2 * sizeof(float));

		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			xtc->delay_buffer[xtc->delay_index]     = temp[idx];
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1];
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			float delayed_left  = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			data[idx]     = fmaxf(fminf(temp[idx]     - xtc->attenuation * delayed_right, 1.0f), -1.0f);
			data[idx + 1] = fmaxf(fminf(temp[idx + 1] - xtc->attenuation * delayed_left,  1.0f), -1.0f);
		}
	} else {
		int16_t *data = (int16_t*)buffer;
		float temp[frames * 2];
		for (int i = 0; i < frames * 2; i++) temp[i] = data[i] / 32768.0f;

		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			xtc->delay_buffer[xtc->delay_index]     = temp[idx];
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1];
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			float delayed_left  = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			temp[idx]     -= xtc->attenuation * delayed_right;
			temp[idx + 1] -= xtc->attenuation * delayed_left;
		}

		for (int i = 0; i < frames * 2; i++) {
			data[i] = (int16_t)fmaxf(fminf(temp[i] * 32767.0f, 32767.0f), -32768.0f);
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
	if (AUDIO_init(&a, dev, sample_rate, channels, FRAMES, 1, format)) {
		printf("Error: Failed to initialize ALSA for stereo output\n");
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
	tui_open();
	print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
	while (1) {
		int k = key(&a, NULL);
		if (k) {
			if (k == 'q' || k == 0x1b) break;
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
		for (int i = 0; i < FRAMES; i++) {
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

			double left  = left_amplitude  * sin(2.0 * M_PI * frequency * t);
			double right = right_amplitude * sin(2.0 * M_PI * frequency * t);

			if (format == SND_PCM_FORMAT_FLOAT_LE) {
				buffer_f32[i * 2]     = (float)left;
				buffer_f32[i * 2 + 1] = (float)right;
			} else {
				buffer_s16[i * 2]     = (int16_t)(left  * 32767.0f);
				buffer_s16[i * 2 + 1] = (int16_t)(right * 32767.0f);
			}
		}

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, output_buffer, FRAMES, channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		global_sample_index += FRAMES;
		phase_sample_index  += FRAMES;
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
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		drwav_uninit(&wav);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, wav.sampleRate, wav.channels);

	float *pcm_buf = (float*)malloc(FRAMES * wav.channels * sizeof(float));

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WAV->DSD(mon)" : "WAV->DSD(DoP)") : (format ? "WAV/F32" : "WAV");
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : 16;
	ts.channels = wav.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = wav.totalPCMFrameCount > 0 ? (double)wav.totalPCMFrameCount / wav.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n;
	while ((n = drwav_read_pcm_frames_f32(&wav, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, wav.channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

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
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WAV->DSD(mon)" : "WAV->DSD(DoP)") : (format ? "WAV/F32" : "WAV");
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * wav.sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (wav.totalPCMFrameCount > 0 && (uint64_t)target > wav.totalPCMFrameCount)
				target = (int64_t)wav.totalPCMFrameCount;
			if (drwav_seek_to_pcm_frame(&wav, (drwav_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / wav.sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / wav.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		ts.note = (n != FRAMES) ? "! short read" : NULL;
		tui_render(&ts);
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

static int fx_seek_to_frame(FILE *f, fx_flac_t *flac, uint64_t target_frame, uint32_t channels)
{
	rewind(f);
	fx_flac_reset(flac);

	uint8_t in_buf[FX_IN_BUF_SIZE];
	uint32_t in_fill = 0;
	int32_t *out_buf = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	if (!out_buf) return 0;

	uint64_t discarded = 0;
	uint64_t target_samples = target_frame * channels;
	fx_flac_state_t state = FLAC_INIT;

	while (discarded < target_samples) {
		if (in_fill < FX_IN_BUF_SIZE && !feof(f)) {
			size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
			in_fill += (uint32_t)rd;
		}
		if (in_fill == 0) break;

		uint32_t in_len = in_fill;
		uint32_t out_len = FX_OUT_SAMPLES;
		state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
		memmove(in_buf, in_buf + in_len, in_fill - in_len);
		in_fill -= in_len;

		if (state == FLAC_ERR) break;
		discarded += out_len;
		if (in_len == 0 && out_len == 0) break;
	}

	free(out_buf);
	return state != FLAC_ERR;
}

void play_flac(char *name, int format, int flag)
{
	FILE *f = fopen(name, "rb");
	if (!f) {
		fprintf(stderr, "Failed to open FLAC: %s\n", name);
		return;
	}

	fx_flac_t *flac = FX_FLAC_ALLOC_DEFAULT();
	if (!flac) { fclose(f); return; }
	fx_flac_reset(flac);

	int32_t *out_buf = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	if (!out_buf) { free(flac); fclose(f); return; }

	uint8_t in_buf[FX_IN_BUF_SIZE];
	uint32_t in_fill = 0;

	fx_flac_state_t state = FLAC_INIT;
	while (state < FLAC_END_OF_METADATA) {
		if (in_fill < FX_IN_BUF_SIZE) {
			size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
			in_fill += (uint32_t)rd;
		}
		if (in_fill == 0) {
			fprintf(stderr, "FLAC: EOF before metadata: %s\n", name);
			goto done;
		}
		uint32_t in_len = in_fill;
		uint32_t out_len = FX_OUT_SAMPLES;
		state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
		memmove(in_buf, in_buf + in_len, in_fill - in_len);
		in_fill -= in_len;
		if (state == FLAC_ERR) {
			fprintf(stderr, "FLAC: metadata error: %s\n", name);
			goto done;
		}
	}

	{
	uint32_t sample_rate = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_SAMPLE_RATE);
	uint32_t channels    = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_N_CHANNELS);
	uint32_t bits        = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_SAMPLE_SIZE);
	int64_t  n_samples   = fx_flac_get_streaminfo(flac, FLAC_KEY_N_SAMPLES);

	OutRouter router;
	if (outr_init(&router, dev, sample_rate, channels, format,
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) goto done;

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, sample_rate, channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : bits;
	ts.channels = channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = n_samples > 0 ? (double)n_samples / sample_rate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	int32_t *pcm_acc = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	float *pcm_f32 = malloc(FRAMES * channels * sizeof(float));
	if (!pcm_acc || !pcm_f32) {
		outr_close(&router); free_crosstalk_cancellation(&xtc);
		free(pcm_acc); free(pcm_f32); goto done;
	}
	uint32_t acc_fill = 0;
	int eof = 0;
	uint64_t frame_pos = 0;
	uint32_t frames_ch = FRAMES * channels;

	tui_open();
	for (;;) {
		while (!eof && acc_fill < frames_ch) {
			if (in_fill < FX_IN_BUF_SIZE && !feof(f)) {
				size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
				in_fill += (uint32_t)rd;
			}
			if (in_fill == 0) { eof = 1; break; }

			uint32_t in_len = in_fill;
			uint32_t out_len = FX_OUT_SAMPLES;
			state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
			memmove(in_buf, in_buf + in_len, in_fill - in_len);
			in_fill -= in_len;

			if (state == FLAC_ERR) { eof = 1; break; }
			if (out_len > 0) {
				uint32_t space = FX_OUT_SAMPLES - acc_fill;
				if (out_len > space) out_len = space;
				memcpy(pcm_acc + acc_fill, out_buf, out_len * sizeof(int32_t));
				acc_fill += out_len;
			}
			if (in_len == 0 && out_len == 0) {
				if (feof(f) && in_fill == 0) eof = 1;
				break;
			}
		}

		if (acc_fill < frames_ch) break;

		for (uint32_t i = 0; i < frames_ch; i++)
			pcm_f32[i] = (float)pcm_acc[i] * (1.0f / 2147483648.0f);

		memmove(pcm_acc, pcm_acc + frames_ch, (acc_fill - frames_ch) * sizeof(int32_t));
		acc_fill -= frames_ch;

		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(&xtc, pcm_f32, FRAMES, channels, SND_PCM_FORMAT_FLOAT_LE);

		outr_feed_float(&router, pcm_f32, FRAMES);

		frame_pos += FRAMES;
		ts.cur = (double)frame_pos / sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		tui_render(&ts);

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
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * sample_rate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)frame_pos + delta;
			if (target < 0) target = 0;
			if (n_samples > 0 && target > n_samples) target = n_samples;
			if (fx_seek_to_frame(f, flac, (uint64_t)target, channels)) {
				frame_pos = (uint64_t)target;
				acc_fill = 0;
				in_fill = 0;
				eof = 0;
				ts.cur = (double)frame_pos / sample_rate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}
	}
	tui_close();

	outr_close(&router);
	free_crosstalk_cancellation(&xtc);
	free(pcm_acc);
	free(pcm_f32);
	}
done:
	free(out_buf);
	free(flac);
	fclose(f);
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
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		drflac_close(flac);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, flac->sampleRate, flac->channels);

	float *pcm_buf = (float*)malloc(FRAMES * flac->channels * sizeof(float));

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : flac->bitsPerSample;
	ts.channels = flac->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = flac->totalPCMFrameCount > 0 ? (double)flac->totalPCMFrameCount / flac->sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n;
	while ((n = drflac_read_pcm_frames_f32(flac, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, flac->channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

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
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * flac->sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (flac->totalPCMFrameCount > 0 && (uint64_t)target > flac->totalPCMFrameCount)
				target = (int64_t)flac->totalPCMFrameCount;
			if (drflac_seek_to_pcm_frame(flac, (drflac_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / flac->sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / flac->sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		tui_render(&ts);
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

	AUDIO a;
	if (AUDIO_init(&a, dev, decoder->sample_rate_pcm, decoder->channels, FRAMES, 1, format)) {
		dsd_decoder_free(decoder);
		fclose(f);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, decoder->sample_rate_pcm, decoder->channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = (decoder->file_type == DSD_FILE_DFF) ? "DFF" : "DSF";
	ts.rate = decoder->sample_rate_pcm;
	ts.bits = format == SND_PCM_FORMAT_FLOAT_LE ? 32 : 16;
	ts.channels = decoder->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = decoder->totalPCMFrameCount > 0 ?
	           (double)decoder->totalPCMFrameCount / decoder->sample_rate_pcm : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t frames_played = 0;
	tui_open();
	size_t n;
	while ((n = dsd_decoder_read_pcm_frames(decoder, a.frames, a.buffer, format)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, a.buffer, n, decoder->channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		int k = key(&a, &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
			g_flag_diff ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = (decoder->file_type == DSD_FILE_DFF) ? "seek not supported for DFF" : "seek not supported for DSF";
			tui_render(&ts);
		} else if (k) {
			break;
		}

		frames_played += n;
		ts.cur = (double)frames_played / decoder->sample_rate_pcm;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		ts.note = (n != a.frames) ? "! short read (end of file)" : NULL;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
	dsd_decoder_free(decoder);
	fclose(f);
	free_crosstalk_cancellation(&xtc);
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
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		drmp3_uninit(&mp3);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, mp3.sampleRate, mp3.channels);

	float *pcm_buf = (float*)malloc(FRAMES * mp3.channels * sizeof(float));
	drmp3_uint64 totalPCMFrameCount = drmp3_get_pcm_frame_count(&mp3);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "MP3->DSD(mon)" : "MP3->DSD(DoP)") : "MP3";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : 16;
	ts.channels = mp3.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = totalPCMFrameCount > 0 ? (double)totalPCMFrameCount / mp3.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n;
	while ((n = drmp3_read_pcm_frames_f32(&mp3, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, mp3.channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

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
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "MP3->DSD(mon)" : "MP3->DSD(DoP)") : "MP3";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * mp3.sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (totalPCMFrameCount > 0 && (uint64_t)target > totalPCMFrameCount)
				target = (int64_t)totalPCMFrameCount;
			if (drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / mp3.sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / mp3.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		tui_render(&ts);
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
	unsigned char *stream_pos = (unsigned char *)file_data;
	int bytes_left = len - 100;

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

	CrosstalkCancel xtc;
	if (flag & USE_CROSSTALK) {
		init_crosstalk_cancellation(&xtc, info.sample_rate, info.channels);
		printf(" with Crosstalk Cancellation\n");
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0) && !key(outr_audio(&router), NULL)) {
		printf("\r%d", c);

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
	printf("\e[?25h");

	outr_close(&router);
	mp3_free(mp3);
	munmap(file_data, len);
	if (flag & USE_CROSSTALK) {
		free_crosstalk_cancellation(&xtc);
	}
	return 0;
}
#endif

void play_ogg(char *name, int flag)
{
	int n, error;
	short outputs[FRAMES*2*100];

	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);
	if (!v) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, v->sample_rate, v->channels, 0,
	              (flag & USE_DSD_ENCODE) ? 1 : 0, (flag & USE_SUPER_RES) ? 1 : 0)) {
		stb_vorbis_close(v);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, v->sample_rate, v->channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "OGG->DSD(mon)" : "OGG->DSD(DoP)") : "OGG";
	ts.rate = outr_output_rate(&router);
	ts.channels = v->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	while ((n = stb_vorbis_get_frame_short_interleaved(v, v->channels, outputs, FRAMES*100))) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, outputs, n, v->channels, 0);
		}

		outr_feed_s16(&router, outputs, (size_t)n);

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
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "OGG->DSD(mon)" : "OGG->DSD(DoP)") : "OGG";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * v->sample_rate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) target = 0;
			if (stb_vorbis_seek(v, (unsigned int)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / v->sample_rate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}
		c += n;
		ts.cur = (double)c / v->sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		tui_render(&ts);
	}
	tui_close();

	outr_close(&router);
	stb_vorbis_close(v);
	free_crosstalk_cancellation(&xtc);
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

	tui_open();
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

		int frames = out_size / (sizeof(short) * cc.channels);
		if (frames <= 0 || frames > MAX_WMA_FRAME_LEN || cc.channels == 0) {
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
			break;
		}

		ts.cur_raw = (uint64_t)(len - bytes_left);
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc_attenuation;
		tui_render(&ts);
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

    tui_open();
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
            break;
        }

        ts.cur_raw = (uint64_t)(stream_pos - file_data);
        ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
        ts.xtc_atten = xtc_attenuation;
        tui_render(&ts);
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
	int num, back = 0;
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
			if (regexp) {
				const char *error;
				Reprog *p = regcomp(regexp, REG_ICASE, &error);
				if (!p) { fprintf(stderr, "regcomp: %s\n", error); free(ls); return; }
				Resub m;
				if (regexec(p, ls[i].d_name, &m, 0)) continue;
			}

			track_index = i + 1;
			track_total = num;
			snprintf(path, 1024, "%s", ls[i].d_name);
			if (access(ls[i].d_name, F_OK) < 0) continue;

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
	for (int i = 0; i < 256; i++) {
		snprintf(buff, 255, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
		FILE *fp = fopen(buff, "w");
		if (!fp) continue;
		fprintf(fp, "%s", c);
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
