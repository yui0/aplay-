#ifndef APLAY_ENGINE_H
#define APLAY_ENGINE_H
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/select.h>
#include <math.h>
#include <time.h>
#include <errno.h>

typedef struct {
    const char *filename;
    const char *dir;
    const char *codec;
    const char *note;
    const char *device;
    const char *format_filter;
    int rate, bits, channels;
    double cur, total;
    float volume;
    int paused;
    int loop_mode;
    int xtc_on;
    float xtc_atten;
    int track_index, track_total;
} tui_state_t;

typedef struct { char d_name[PATH_MAX]; } LS_LIST;
typedef struct { void *handle; } AUDIO;

#define USE_FLOAT32     (1u << 0)
#define USE_CROSSTALK   (1u << 1)
#define USE_DSD_ENCODE  (1u << 2)
#define USE_SUPER_RES   (1u << 3)
#define USE_TEST_MODE   (1u << 4)
#define LS_RECURSIVE    (1u << 5)
#define LS_RANDOM       (1u << 6)
#define SND_PCM_FORMAT_FLOAT_LE 1
#define APLAY_EQ_BANDS 10
#define KEY_LEFT  1001
#define KEY_RIGHT 1002
#define KEY_UP    1003
#define KEY_DOWN  1004

static int g_flag_diff = 0;
static int g_device_select_idx = -1;
static char g_dev_storage[64] = "hw:0,0";
static FILE *g_dsd_raw_file = NULL;
static const char *dsd_file_path = NULL;
static float volume = 0.82f;
static int loop_mode = 1;
static int verbose = 0;
static float speaker_distance_m = 0.45f;
static int cmd = 0;

static const char *fmt_cycle[] = {
    NULL, "flac", "mp3", "m4a|aac", "ogg", "wav", "wma", "dsf", "dff"
};
static const int fmt_cycle_n = (int)(sizeof(fmt_cycle) / sizeof(fmt_cycle[0]));
static int fmt_filter_idx = 0;
static char *fmt_filter = NULL;

static int aplay_device_count(void) { return 4; }
static const char *aplay_device_name(int i) {
    static const char *v[] = { "hw:0,0", "hw:1,0", "hw:2,0", "default" };
    return (i >= 0 && i < 4) ? v[i] : NULL;
}
static const char *aplay_device_label(int i) {
    static const char *v[] = { "Built-in Audio — hw:0,0", "USB DAC — hw:1,0", "Studio Interface — hw:2,0", "System Default" };
    return (i >= 0 && i < 4) ? v[i] : NULL;
}
static int aplay_refresh_devices(void) { return 4; }
static void aplay_copy_dev(const char *s) { if (s) snprintf(g_dev_storage, sizeof(g_dev_storage), "%s", s); }
static void aplay_auto_select_device(void) { aplay_copy_dev("hw:1,0"); }

static int g_eq_enabled = 1;
static float g_eq_preamp = 0.0f;
static float g_eq_bands[APLAY_EQ_BANDS] = { 2.0f, 1.0f, 0.0f, -1.0f, -1.5f, 0.0f, 1.5f, 2.0f, 1.0f, 0.0f };
static const float g_eq_freqs[APLAY_EQ_BANDS] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static int aplay_equalizer_enabled(void) { return g_eq_enabled; }
static void aplay_equalizer_set_enabled(int v) { g_eq_enabled = !!v; }
static void aplay_equalizer_reset(void) { g_eq_preamp = 0; memset(g_eq_bands, 0, sizeof(g_eq_bands)); }
static float aplay_equalizer_preamp(void) { return g_eq_preamp; }
static void aplay_equalizer_set_preamp(float v) { g_eq_preamp = v; }
static float aplay_equalizer_band(int i) { return (i >= 0 && i < APLAY_EQ_BANDS) ? g_eq_bands[i] : 0; }
static void aplay_equalizer_set_band(int i, float v) { if (i >= 0 && i < APLAY_EQ_BANDS) g_eq_bands[i] = v; }
static float aplay_equalizer_frequency(int i) { return (i >= 0 && i < APLAY_EQ_BANDS) ? g_eq_freqs[i] : 0; }

static int aplay_device_unique_cards(int *out, int max) {
    int n = max < 3 ? max : 3;
    for (int i = 0; i < n; i++) out[i] = i;
    return n;
}
static int aplay_device_card(int i) { return (i >= 0 && i < 3) ? i : -1; }
static const char *aplay_device_card_name(int i) {
    static const char *v[] = { "Built-in Audio", "USB DAC", "Studio Interface" };
    return (i >= 0 && i < 3) ? v[i] : "Audio Device";
}
static int aplay_device_pcm(int i) { return (i >= 0 && i < 3) ? 0 : -1; }

static uint64_t screenshot_rng = 0x9e3779b97f4a7c15ULL;
static void xoroshiro128plus_init(uint64_t seed) { screenshot_rng = seed ? seed : 1; }
static uint64_t xoroshiro128plus(void) {
    screenshot_rng ^= screenshot_rng << 13;
    screenshot_rng ^= screenshot_rng >> 7;
    screenshot_rng ^= screenshot_rng << 17;
    return screenshot_rng;
}

static LS_LIST *ls_dir(char *dir, int flag, int *num) {
    (void)dir; (void)flag; if (num) *num = 0; return NULL;
}
static const char *findExt(const char *path) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot + 1 : "";
}

#endif
