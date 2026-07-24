// ©2017-2026 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound                          (dr_flac, default)
// clang -Os -DUSE_FOXEN_FLAC -o aplay+ aplay+.c -lasound         (foxen-flac)
// Generate flac.h (required for foxen-flac): ./make_flac_h.sh

#include <stdio.h>
#include <sys/mman.h>
#include <string.h> // for memcpy
#include <math.h>   // for sin
#include <fcntl.h>  // for open, lseek
#include <unistd.h> // for close
#include <limits.h> // for PATH_MAX

#include "alsa.h"
#ifdef USE_FOXEN_FLAC
#define FLAC_IMPLEMENTATION
#include "flac.h"
#else
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#endif
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#ifdef DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#else
#include "minimp3.h"
#endif
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR
//#define AAC_ENABLE_SBR
#include "uaac.h"
#include "uwma.h"
#include "stb_vorbis.h"
#define DSD_DECODER_IMPLEMENTATION
#include "dsd.h"

#define PARG_IMPLEMENTATION
#include "parg.h"
#include "regexp.h"

#include "random.h"
#include "ls.h"
#include "kbhit.h"
#include "tui.h"

int verbose = 0;
float volume = 1.0f;
int loop_mode = 0;
float speaker_distance_m = 0.5;
int cmd;
int track_index = 0, track_total = 0; // current position in the playlist, for the TUI panel

// Interactive format filter, cycled with the 'f' key during playback (see
// key() below). NULL means "ALL" (no filtering). play_dir() re-checks this
// on every file in its listing, so a change takes effect from the very next
// track without needing to restart the player.
static const char *fmt_cycle[] = { NULL, "flac", "mp3", "m4a", "ogg", "wav", "wma", "dsf", "dff" };
static const int fmt_cycle_n = sizeof(fmt_cycle) / sizeof(fmt_cycle[0]);
static int fmt_filter_idx = 0;
char *fmt_filter = NULL;

//char *dev = "default";  // "plughw:0,0"
char *dev = "hw:0,0";  // BitPerfect

// Synthetic key codes for cursor keys, well outside the char range so they
// can never collide with a real byte read from the terminal.
#define KEY_UP     1001
#define KEY_DOWN   1002
#define KEY_LEFT   1003
#define KEY_RIGHT  1004

#define SEEK_SECONDS   10      // amount of fast-forward/rewind per Left/Right press
#define VOLUME_STEP    0.05f   // amount of volume change per Up/Down press

// Returns a pointer to the filename portion of `path`, stripping any
// leading directory components (everything up to and including the last
// '/'). Used so the TUI marquee shows only the file name, not the full path.
static const char *get_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// Extracts the parent directory portion of path into dir (size bytes).
// "music/rock/song.flac" -> "music/rock", "song.flac" -> "."
// (defined further down, forward-declared here so the play_* functions
// above it can use it to feed tui_state_t.dir for the TUI title line)
static void get_dirpart(const char *path, char *dir, size_t size);

// Pushes the current `volume` (0.0-1.0) out to the ALSA hardware mixer so
// decoded PCM samples stay bit-exact (no software gain on the buffer).
void apply_alsa_volume(void)
{
	AUDIO_set_volume(dev, volume);
}

// `ts`, if non-NULL, is re-rendered immediately on pause/resume so the TUI
// panel reflects the paused state instead of freezing silently.
int key(AUDIO *a, tui_state_t *ts)
{
	if (!kbhit()) {
		return 0;
	}

	int c = cmd = getchar();
	if (verbose) {
		printf("%x\n", c);
	}

	// Cursor keys arrive as multi-byte ANSI escape sequences: ESC '[' <A|B|C|D>.
	// A lone Esc (meant to quit) never has more bytes following it, while a
	// real terminal emits the whole 3-byte sequence in one burst, so a short
	// bounded poll is enough to tell the two apart.
	if (c == 0x1b) {
		int tries = 0;
		while (!kbhit() && tries < 20) {
			usleep(500);
			tries++;
		}
		if (kbhit()) {
			int c2 = getchar();
			if (c2 == '[' || c2 == 'O') {
				tries = 0;
				while (!kbhit() && tries < 20) {
					usleep(500);
					tries++;
				}
				if (kbhit()) {
					int c3 = getchar();
					switch (c3) {
					case 'A':
						c = KEY_UP;
						break;
					case 'B':
						c = KEY_DOWN;
						break;
					case 'C':
						c = KEY_RIGHT;
						break;
					case 'D':
						c = KEY_LEFT;
						break;
					default:
						c = 0; // unrecognized escape sequence, ignore
						break;
					}
				} else {
					c = 0; // incomplete sequence, drop it
				}
			}
			// else: not an escape sequence we understand; c2 is dropped and
			// the original Esc (c == 0x1b) still falls through as Quit below.
		}
		// else: no more bytes followed -> lone Esc, falls through as Quit.
		cmd = c;
	}

	if (c == 'f') {
		// Cycle to the next format filter (ALL -> flac -> mp3 -> ... -> ALL).
		// Takes effect from the next track play_dir() considers; the current
		// track keeps playing.
		fmt_filter_idx = (fmt_filter_idx + 1) % fmt_cycle_n;
		fmt_filter = (char *)fmt_cycle[fmt_filter_idx];
		if (ts) {
			ts->format_filter = fmt_filter;
			ts->note = fmt_filter ? fmt_filter : "ALL";
			tui_render(ts);
		}
		return 0;
	}

	if (c == KEY_UP || c == KEY_DOWN) {
		volume += (c == KEY_UP ? VOLUME_STEP : -VOLUME_STEP);
		if (volume > 1.0f) {
			volume = 1.0f;
		}
		if (volume < 0.0f) {
			volume = 0.0f;
		}
		apply_alsa_volume();
		if (ts) {
			ts->volume = volume;
			tui_render(ts);
		}
		return 0;
	}

	if (c==0x09) { // Tab
		// Quick pause: keeps the device open, resumes instantly, but the
		// sound card stays held by us the whole time.
		snd_pcm_pause(a->handle, 1);
		if (ts) {
			ts->paused = 1;
			tui_render(ts);
		}
		do {
			usleep(1000); // us
		} while (!kbhit());
		getchar(); // clear
		cmd = 0;
		snd_pcm_pause(a->handle, 0);
		snd_pcm_prepare(a->handle);
		if (ts) {
			ts->paused = 0;
			tui_render(ts);
		}
		return 0;
	}

	if (c==0x20) { // Space
		// Release pause: actually gives up the device (snd_pcm_close) so
		// another application can use the sound card while we're paused.
		// Slower to resume than Tab, since the device has to be reopened.
		AUDIO_release(a);
		if (ts) {
			ts->paused = 1;
			tui_render(ts);
		}
		do {
			usleep(1000); // us
		} while (!kbhit());
		getchar(); // clear
		cmd = 0;
		AUDIO_reopen(a);
		apply_alsa_volume(); // mixer setting is lost if the device changed hands
		if (ts) {
			ts->paused = 0;
			tui_render(ts);
		}
		return 0;
	}

	return c;
}

#define USE_FLOAT32   128
#define USE_CROSSTALK 256 // Flag for crosstalk cancellation
#define USE_TEST_MODE 512 // Flag for test mode
#define FRAMES        32
//#define FRAMES        128
#define MAX_DELAY_SAMPLES 16 // Maximum delay samples for crosstalk (e.g., 71µs at 44.1kHz is ~3 samples)

// Crosstalk cancellation parameters
typedef struct {
	int delay_samples;    // Delay in samples (e.g., 3 for 71µs at 44.1kHz)
	float attenuation;    // Attenuation factor (e.g., 0.9)
	float *delay_buffer;  // Buffer to store delayed samples
	int delay_buffer_size; // Size of delay buffer
	int delay_index;      // Current index in delay buffer
} CrosstalkCancel;

/*void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
    if (channels != 2) return; // Only support stereo
    xtc->delay_samples = (int)(sample_rate * 0.000071); // 71µs delay
    xtc->attenuation = 0.4f; // Natural effect
    xtc->delay_buffer_size = xtc->delay_samples * 2; // Stereo
    if (xtc->delay_buffer_size < 2) xtc->delay_buffer_size = 2; // Ensure at least 2 samples for stereo
    xtc->delay_buffer = (float*)calloc(xtc->delay_buffer_size, sizeof(float));
    xtc->delay_index = 0;
}*/
void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
	if (channels != 2) {
		return;
	}
	// 音速343m/sを基準に、スピーカー間距離から遅延を計算
	xtc->delay_samples = (int)(sample_rate * (speaker_distance_m / 343.0));
	xtc->attenuation = 0.4f; // デフォルト値
	xtc->delay_buffer_size = xtc->delay_samples * 2;
	if (xtc->delay_buffer_size < 2) {
		xtc->delay_buffer_size = 2;
	}
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
	if (channels != 2) {
		return;        // Only support stereo
	}

	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		float *data = (float*)buffer;
		float temp[frames * 2];
		memcpy(temp, data, frames * 2 * sizeof(float));

		// Process crosstalk cancellation
		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			// Store current samples in delay buffer
			xtc->delay_buffer[xtc->delay_index] = temp[idx];     // Left
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1]; // Right
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			// Apply crosstalk cancellation
			float delayed_left = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			float new_left = temp[idx] - xtc->attenuation * delayed_right;
			float new_right = temp[idx + 1] - xtc->attenuation * delayed_left;

			// Clipping prevention
			/*if (new_left > 1.0f) new_left = 1.0f;
			if (new_left < -1.0f) new_left = -1.0f;
			if (new_right > 1.0f) new_right = 1.0f;
			if (new_right < -1.0f) new_right = -1.0f;
			data[idx] = new_left;
			data[idx + 1] = new_right;*/
			data[idx] = fmaxf(fminf(new_left, 1.0f), -1.0f);
			data[idx + 1] = fmaxf(fminf(new_right, 1.0f), -1.0f);
		}
	} else { // SND_PCM_FORMAT_S16_LE
		/*int16_t *data = (int16_t*)buffer;
		float temp[frames * 2];
		float original_temp[frames * 2]; // Store original signal

		// Convert int16 to float for processing
		for (int i = 0; i < frames * 2; i++) {
		    original_temp[i] = data[i] / 32768.0f;
		}
		memcpy(temp, original_temp, frames * 2 * sizeof(float));

		// Process crosstalk cancellation
		for (int i = 0; i < frames; i++) {
		    int idx = i * 2;
		    int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

		    // Store current samples in delay buffer
		    xtc->delay_buffer[xtc->delay_index] = original_temp[idx];     // Left
		    xtc->delay_buffer[xtc->delay_index + 1] = original_temp[idx + 1]; // Right
		    xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

		    // Apply crosstalk cancellation
		    float delayed_left = xtc->delay_buffer[delay_idx];
		    float delayed_right = xtc->delay_buffer[delay_idx + 1];

		    temp[idx] = original_temp[idx] - xtc->attenuation * delayed_right;
		    temp[idx + 1] = original_temp[idx + 1] - xtc->attenuation * delayed_left;
		}

		for (int i = 0; i < frames * 2; i++) {
		    float val = temp[i] * 32767.0f; // Use 32767.0f to avoid overflow
		    if (val > 32767.0f) val = 32767.0f;
		    if (val < -32768.0f) val = -32768.0f;
		    data[i] = (int16_t)val;
		}*/
		int16_t *data = (int16_t*)buffer;
		float temp[frames * 2];
		for (int i = 0; i < frames * 2; i++) {
			temp[i] = data[i] / 32768.0f;
		}

		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			xtc->delay_buffer[xtc->delay_index] = temp[idx];
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1];
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			float delayed_left = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			temp[idx] = temp[idx] - xtc->attenuation * delayed_right;
			temp[idx + 1] = temp[idx + 1] - xtc->attenuation * delayed_left;
		}

		for (int i = 0; i < frames * 2; i++) {
			float val = temp[i] * 32767.0f;
			data[i] = (int16_t)fmaxf(fminf(val, 32767.0f), -32768.0f);
		}
	}
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
	ts.xtc_atten = xtc->attenuation;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;
	ts.note = "[+/-] adjust attenuation";
	tui_render(&ts);
}

void play_test_mode(int format, int flag)
{
	const int sample_rate = 44100; // Standard sample rate
	const int channels = 2;        // Stereo
	const double duration = 5.0;   // 5 seconds per phase
	const int frames_per_phase = (int)(sample_rate * duration);
	const int total_phases = 3;    // Left, Right, Pan
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
			if (k == 'q' || k == 0x1b) {
				break;
			}
			if (k == 'c') {
				flag ^= USE_CROSSTALK;
			}
			if (k == '+' || k == '=') {
				xtc.attenuation += 0.05f;
				if (xtc.attenuation > 1.0f) {
					xtc.attenuation = 1.0f;
				}
			}
			if (k == '-') {
				xtc.attenuation -= 0.05f;
				if (xtc.attenuation < 0.0f) {
					xtc.attenuation = 0.0f;
				}
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

			if (phase == 0) { // Left channel only
				left_amplitude = 0.5;
				right_amplitude = 0.0;
			} else if (phase == 1) { // Right channel only
				left_amplitude = 0.0;
				right_amplitude = 0.5;
			} else { // Pan from left to right
				double pan = (double)current_phase_sample / frames_per_phase;
				if (pan > 1.0) {
					pan = 1.0;
				}
				left_amplitude = 0.5 * (1.0 - pan);
				right_amplitude = 0.5 * pan;
			}

			double left = left_amplitude * sin(2.0 * M_PI * frequency * t);
			double right = right_amplitude * sin(2.0 * M_PI * frequency * t);

			if (format == SND_PCM_FORMAT_FLOAT_LE) {
				buffer_f32[i * 2] = (float)left;
				buffer_f32[i * 2 + 1] = (float)right;
			} else {
				buffer_s16[i * 2] = (int16_t)(left * 32767.0f);
				buffer_s16[i * 2 + 1] = (int16_t)(right * 32767.0f);
			}
		}

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, output_buffer, FRAMES, channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		global_sample_index += FRAMES;
		phase_sample_index += FRAMES;
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

void display_progress(uint64_t current, uint64_t total, int is_time, const char *label)
{
	if (total == 0) {
		return;        // Skip if unknown
	}
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

void play_wav(char *name, int format, int flag)
{
	drwav wav;
	if (drwav_init_file(&wav, name, NULL)) {
		AUDIO a;
		if (AUDIO_init(&a, dev, wav.sampleRate, wav.channels, FRAMES, 1, format)) {
			return;
		}

		CrosstalkCancel xtc;
		init_crosstalk_cancellation(&xtc, wav.sampleRate, wav.channels);

		uint64_t (*func)(drwav* pWav, drwav_uint64 framesToRead, void* pBufferOut);
		if (format) {
			func = (uint64_t (*)(drwav *, drwav_uint64, void *))drwav_read_pcm_frames_f32;
		} else {
			func = (uint64_t (*)(drwav *, drwav_uint64, void *))drwav_read_pcm_frames_s16;
		}

		tui_state_t ts = {0};
		ts.track_index = track_index;
		ts.track_total = track_total;
		ts.filename = get_basename(name);
		char dirbuf[PATH_MAX];
		get_dirpart(name, dirbuf, sizeof(dirbuf));
		ts.dir = dirbuf;
		ts.codec = format ? "WAV/F32" : "WAV";
		ts.rate = wav.sampleRate;
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
		size_t n; // numberOfSamplesActuallyDecoded
		while ((n = func(&wav, a.frames, (drwav_int16*)a.buffer)) > 0) {
			if (flag & USE_CROSSTALK) {
				apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, wav.channels, format);
			}
			AUDIO_play0(&a);
			AUDIO_wait(&a, 100);
			int k = key(&a, &ts);
			if (k=='c') {
				flag ^= USE_CROSSTALK;
			} else if (k == KEY_LEFT || k == KEY_RIGHT) {
				int64_t delta = (int64_t)SEEK_SECONDS * wav.sampleRate * (k == KEY_RIGHT ? 1 : -1);
				int64_t target = (int64_t)c + delta;
				if (target < 0) {
					target = 0;
				}
				if (wav.totalPCMFrameCount > 0 && (uint64_t)target > wav.totalPCMFrameCount) {
					target = (int64_t)wav.totalPCMFrameCount;
				}
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
			ts.xtc_atten = xtc.attenuation;
			ts.note = (n != a.frames) ? "! short read" : NULL;
			tui_render(&ts);
		}
		tui_close();

		AUDIO_close(&a);
		drwav_uninit(&wav);
		free_crosstalk_cancellation(&xtc);
	}
}

#ifdef USE_FOXEN_FLAC

#include <stdlib.h>

#define FX_IN_BUF_SIZE  8192
/* One Subset block × max channels; covers all common FLAC files. */
#define FX_OUT_SAMPLES  (FLAC_SUBSET_MAX_BLOCK_SIZE * FLAC_MAX_CHANNEL_COUNT)

/* Seek to target_frame by rewinding the file and decoding forward.
 * foxen-flac has no native seek; this is O(target) but acceptable for
 * a music player where seeks are at most a few seconds at a time. */
static int fx_seek_to_frame(FILE *f, fx_flac_t *flac, uint64_t target_frame,
                            uint32_t channels)
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
		if (in_len == 0 && out_len == 0) break; /* no progress guard */
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

	/* Phase 1: pump decoder until stream info is available. */
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

	AUDIO a;
	if (AUDIO_init(&a, dev, sample_rate, channels, FRAMES, 1, format)) goto done;

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, sample_rate, channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = "FLAC";
	ts.rate = sample_rate;
	ts.bits = format ? 32 : bits;
	ts.channels = channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = n_samples > 0 ? (double)n_samples / sample_rate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	/* Accumulation buffer: foxen-flac decodes one block at a time
	 * (up to FX_OUT_SAMPLES interleaved samples).  We drain it FRAMES
	 * frames at a time to match the ALSA period size. */
	int32_t *pcm_acc = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	if (!pcm_acc) { AUDIO_close(&a); free_crosstalk_cancellation(&xtc); goto done; }
	uint32_t acc_fill = 0;
	int eof = 0;

	uint64_t frame_pos = 0;
	uint32_t frames_ch = FRAMES * channels;

	tui_open();
	for (;;) {
		/* Refill accumulation buffer. */
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
				break; /* no progress — try again after draining ALSA */
			}
		}

		if (acc_fill < frames_ch) break;

		/* Convert one ALSA period from int32_t to target format.
		 * foxen-flac left-shifts samples to fill all 32 bits. */
		if (format) {
			float *dst = (float *)a.buffer;
			for (uint32_t i = 0; i < frames_ch; i++)
				dst[i] = (float)pcm_acc[i] * (1.0f / 2147483648.0f);
		} else {
			int16_t *dst = (int16_t *)a.buffer;
			for (uint32_t i = 0; i < frames_ch; i++)
				dst[i] = (int16_t)(pcm_acc[i] >> 16);
		}

		memmove(pcm_acc, pcm_acc + frames_ch, (acc_fill - frames_ch) * sizeof(int32_t));
		acc_fill -= frames_ch;

		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(&xtc, a.buffer, FRAMES, channels, format);
		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		frame_pos += FRAMES;
		ts.cur = (double)frame_pos / sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);

		int k = key(&a, &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
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

	AUDIO_close(&a);
	free_crosstalk_cancellation(&xtc);
	free(pcm_acc);
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

	AUDIO a;
	if (AUDIO_init(&a, dev, flac->sampleRate, flac->channels, FRAMES, 1, format)) {
		drflac_close(flac);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, flac->sampleRate, flac->channels);

	uint64_t (*func)(drflac* pFlac, drflac_uint64 framesToRead, void* pBufferOut);
	if (format) {
		func = (uint64_t (*)(drflac *, drflac_uint64, void *))drflac_read_pcm_frames_f32;
	} else {
		func = (uint64_t (*)(drflac *, drflac_uint64, void *))drflac_read_pcm_frames_s16;
	}

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = "FLAC";
	ts.rate = flac->sampleRate;
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
	size_t n; // numberOfSamplesActuallyDecoded
	while ((n = func(flac, a.frames, (void*)a.buffer)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, flac->channels, format);
		}
		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);
		int k = key(&a, &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * flac->sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (flac->totalPCMFrameCount > 0 && (uint64_t)target > flac->totalPCMFrameCount) {
				target = (int64_t)flac->totalPCMFrameCount;
			}
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
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
	drflac_close(flac);
	free_crosstalk_cancellation(&xtc);
}

#endif /* USE_FOXEN_FLAC */

// Handles both .dsf (Sony DSF) and .dff (Philips DSDIFF) files; dsd.h
// auto-detects the container format from the file's magic bytes.
void play_dsf(char *name, int format, int flag)
{
	FILE *f = fopen(name, "rb");
	if (!f) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	// Initialize DSD decoder from file stream
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
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = (decoder->file_type == DSD_FILE_DFF) ? "seek not supported for DFF" : "seek not supported for DSF";
			tui_render(&ts);
		} else if (k) {
			break;
		}

		frames_played += n;
		ts.cur = (double)frames_played / decoder->sample_rate_pcm;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
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

	AUDIO a;
	if (AUDIO_init(&a, dev, mp3.sampleRate, mp3.channels, FRAMES, 1, format)) {
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, mp3.sampleRate, mp3.channels);

	uint64_t (*func)(drmp3*, drmp3_uint64, void*);
	if (format) {
		func = (uint64_t (*)(drmp3 *, drmp3_uint64, void *))drmp3_read_pcm_frames_f32;
	} else {
		func = (uint64_t (*)(drmp3 *, drmp3_uint64, void *))drmp3_read_pcm_frames_s16;
	}

	drmp3_uint64 totalPCMFrameCount = drmp3_get_pcm_frame_count(&mp3);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = "MP3";
	ts.rate = mp3.sampleRate;
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
	while ((n = func(&mp3, a.frames, (drmp3_int16*)a.buffer)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, mp3.channels, format);
		}
		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);
		int k = key(&a, &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * mp3.sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (totalPCMFrameCount > 0 && (uint64_t)target > totalPCMFrameCount) {
				target = (int64_t)totalPCMFrameCount;
			}
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
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
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
	AUDIO a;
	if (AUDIO_init(&a, dev, info.sample_rate, info.channels, FRAMES, 1, 0)) {
		munmap(file_data, len);
		return 1;
	}

	CrosstalkCancel xtc;
	if (flag & USE_CROSSTALK) {
		init_crosstalk_cancellation(&xtc, info.sample_rate, info.channels);
		printf(" with Crosstalk Cancellation\n");
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0) && !key(&a, NULL)) {
		printf("\r%d", c);

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, sample_buf, info.audio_bytes/2/info.channels, info.channels, 0);
		}
		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
		AUDIO_wait(&a, 100);

		c += frame_size;
		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h");

	AUDIO_close(&a);
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
	int n, num_c, error;
	short outputs[FRAMES*2*100];

	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);
	if (!v) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	AUDIO a;
	if (AUDIO_init(&a, dev, v->sample_rate, v->channels, FRAMES*2, 1, 0)) {
		stb_vorbis_close(v);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, v->sample_rate, v->channels);

	// stb_vorbis doesn't give us a cheap total-sample count up front, so this
	// stream is shown with elapsed time only (no total / no progress bar fill).
	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = "OGG";
	ts.rate = v->sample_rate;
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
		AUDIO_play(&a, (char*)outputs, n);
		AUDIO_wait(&a, 100);
		int k = key(&a, &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * v->sample_rate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
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
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
	stb_vorbis_close(v);
	free_crosstalk_cancellation(&xtc);
}

#define MAX_WMA_FRAME_LEN 4096
#define MAX_WMA_SUBFRAMES 16
#define MAX_PCM_BUFFER_SIZE (MAX_WMA_SUBFRAMES * MAX_WMA_FRAME_LEN * 8 * sizeof(short))  // 8 channels max, ~1MB safe
int play_wma(char *name, int flag)
{
	void *file_data = NULL;
	unsigned char *stream_pos;
	short *sample_buf = NULL;
	int bytes_left;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	int len = lseek(fd, 0, SEEK_END);
	if (len <= 0) {
		fprintf(stderr, "Error: invalid file size\n");
		close(fd);
		return 1;
	}

	lseek(fd, 0, SEEK_SET);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (file_data == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

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
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	WMADecodeContext *s = (WMADecodeContext *)cc.priv_data;
	if (wma_decode_init_fixed(&cc) < 0) {
		fprintf(stderr, "Error: failed to initialize WMA decoder\n");
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	size_t pcm_buffer_size = s->nb_channels * s->frame_len * MAX_WMA_SUBFRAMES * sizeof(short);
	if (pcm_buffer_size == 0 || pcm_buffer_size > MAX_PCM_BUFFER_SIZE || s->nb_channels == 0 || s->frame_len == 0) {
		fprintf(stderr, "Error: invalid PCM buffer parameters: nb_channels=%d, frame_len=%d, pcm_buffer_size=%zu\n",
		        s->nb_channels, s->frame_len, pcm_buffer_size);
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	sample_buf = malloc(pcm_buffer_size);
	if (!sample_buf) {
		fprintf(stderr, "Error: failed to allocate PCM buffer of size %zu bytes\n", pcm_buffer_size);
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	AUDIO a;
	if (AUDIO_init(&a, dev, cc.sample_rate, cc.channels, FRAMES, 1, 0)) {
		fprintf(stderr, "Error: failed to initialize audio\n");
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(sample_buf);
		free(cc.priv_data);
		return 1;
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
	ts.codec = "WMA";
	ts.rate = cc.sample_rate;
	ts.channels = cc.channels;
	ts.device = dev;
	ts.use_time = 0; // decoded frame timing isn't tracked; show raw byte progress instead
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
			if (!sample_buf) {
				fprintf(stderr, "Error: sample_buf is NULL\n");
				break;
			}
			apply_crosstalk_cancellation(&xtc, sample_buf, frames, cc.channels, 0);
		}

		AUDIO_play(&a, (char*)sample_buf, frames);
		AUDIO_wait(&a, 100);

		stream_pos += frame_size;
		bytes_left -= frame_size;

		int k = key(&a, &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = "seek not supported for WMA";
			tui_render(&ts);
		} else if (k) {
			break;
		}

		ts.cur_raw = (uint64_t)(len - bytes_left);
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
	wma_decode_end(&cc);
	munmap(file_data, len);
	free(sample_buf);
	free(cc.priv_data);
	free_crosstalk_cancellation(&xtc);
	return 0;
}

/*int play_aac(char *name, int flag)
{
    unsigned char *file_data;
    unsigned char *stream_pos;
    short sample_buf[AAC_BUF_SIZE*2];
    int bytes_left;

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        printf("Error: cannot open `%s`\n", name);
        return 1;
    }

    int samplerate, channels;
    file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels);
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
    info.profile = AAC_PROFILE_LC;

    HAACDecoder aac = AACInitDecoder();
    AACSetRawBlockParams(aac, 0, &info);

    int output_samplerate = info.sampRateCore;
    int sbr_enabled = 0;
    if (output_samplerate <= 24000) {
        output_samplerate *= 2;
        sbr_enabled = 1;
    }

    printf("%dHz (output: %dHz) %dch\n", info.sampRateCore, output_samplerate, info.nChans);

    AUDIO a;
    if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {
        free(file_data);
        close(fd);
        AACFreeDecoder(aac);
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

    printf("\e[?25l");
    while (bytes_left > 0) {
        int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
        printf("\r%d %d", (int)(stream_pos - file_data), bytes_left);
        if (!r) {
            AACGetLastFrameInfo(aac, &info);
            if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, sample_buf, AAC_MAX_NSAMPS, info.nChans, 0);
            AUDIO_play(&a, (char*)sample_buf, AAC_MAX_NSAMPS);
            AUDIO_wait(&a, 100);
        } else {
            printf("\nAAC decode error %d, attempting resync\n", r);
            if (!sbr_enabled && info.sampRateCore <= 24000) {
                printf("Trying HE-AAC with SBR\n");
                info.sampRateCore *= 2;
                info.profile = 5; // HE-AAC
                AACFreeDecoder(aac);
                aac = AACInitDecoder();
                AACSetRawBlockParams(aac, 0, &info);
                stream_pos = file_data;
                bytes_left = *(&bytes_left);
                continue;
            }
            int nextSync = AACFindSyncWord(stream_pos, bytes_left);
            if (nextSync >= 0) {
                stream_pos += nextSync;
                bytes_left -= nextSync;
                continue;
            } else {
                printf("Failed to resync, stopping\n");
                break;
            }
        }

        int k = key(&a);
        if (k=='c') flag ^= USE_CROSSTALK;
        else if (k) break;
    }
    printf("\e[?25h");

    AUDIO_close(&a);
    AACFreeDecoder(aac);
    free(file_data);
    close(fd);
    free_crosstalk_cancellation(&xtc);
    return 0;
}*/
/*int play_aac(char *name, int flag)
{
	unsigned char *file_data;
	unsigned char *stream_pos;
	short sample_buf[AAC_BUF_SIZE*2];
	int bytes_left;
	int max_resync_attempts = 5;
	int resync_attempts = 0;

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
	    // Assume potential HE-AAC if low rate and not detected
	    sbr_enabled = 1;
	    info.profile = 5;
	    output_samplerate *= 2;
	    printf("Assuming potential HE-AAC with SBR, output samplerate: %dHz\n", output_samplerate);
	}
	printf("%dHz (output: %dHz) %dch\n", info.sampRateCore, output_samplerate, info.nChans);

	AUDIO a;
	if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {
		printf("Error: failed to initialize ALSA with %dHz, %dch\n", output_samplerate, info.nChans);
		free(file_data);
		close(fd);
		AACFreeDecoder(aac);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

	printf("\e[?25l");
	while (bytes_left > 0) {
		int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
		if (verbose) {
			printf("\rDecoded %d bytes, %d bytes left, result=%d\n", (int)(stream_pos - file_data), bytes_left, r);
		} else {
			printf("\r%d/%d", (int)(stream_pos - file_data), bytes_left + (int)(stream_pos - file_data));
		}
		if (!r) {
			resync_attempts = 0; // Reset on successful decode
			AACGetLastFrameInfo(aac, &info);
			if (verbose) {
				printf("Frame: %d samples, %d channels, %dHz\n", AAC_MAX_NSAMPS, info.nChans, info.sampRateCore);
			}
			if (flag & USE_CROSSTALK) {
				apply_crosstalk_cancellation(&xtc, sample_buf, AAC_MAX_NSAMPS, info.nChans, 0);
			}
			AUDIO_play(&a, (char*)sample_buf, AAC_MAX_NSAMPS);
			AUDIO_wait(&a, 100);
		} else {
			printf("\nAAC decode error %d, attempting resync (%d/%d)\n", r, resync_attempts + 1, max_resync_attempts);
			if (!sbr_enabled && info.sampRateCore <= 24000) {
				printf("Trying HE-AAC with SBR\n");
				info.sampRateCore *= 2;
				info.profile = 5; // HE-AAC
				AACFreeDecoder(aac);
				aac = AACInitDecoder();
				AACSetRawBlockParams(aac, 0, &info);
				stream_pos = file_data;
				bytes_left = *(&bytes_left);
				resync_attempts = 0;
				// Reinitialize ALSA with new sample rate
				AUDIO_close(&a);
				if (AUDIO_init(&a, dev, info.sampRateCore, info.nChans, FRAMES, 1, 0)) {
					printf("Error: failed to reinitialize ALSA with %dHz\n", info.sampRateCore);
					free(file_data);
					close(fd);
					AACFreeDecoder(aac);
					free_crosstalk_cancellation(&xtc);
					return 1;
				}
				continue;
			}
			int nextSync = AACFindSyncWord(stream_pos, bytes_left);
			if (nextSync >= 0) {
				stream_pos += nextSync;
				bytes_left -= nextSync;
				resync_attempts = 0;
				continue;
			} else if (++resync_attempts < max_resync_attempts) {
				stream_pos += 1;
				bytes_left -= 1;
				if (verbose) {
					printf("Skipping 1 byte, %d bytes left\n", bytes_left);
				}
				continue;
			} else {
				printf("Failed to resync after %d attempts, stopping\n", max_resync_attempts);
				break;
			}
		}

		int k = key(&a);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k) {
			break;
		}
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	AACFreeDecoder(aac);
	free(file_data);
	close(fd);
	free_crosstalk_cancellation(&xtc);
	return 0;
}*/
int play_aac(char *name, int flag)
{
    unsigned char *file_data;
    unsigned char *stream_pos;
    short sample_buf[AAC_BUF_SIZE * 2];
    int bytes_left;
    int max_resync_attempts = 5;
    int resync_attempts = 0;

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
    AUDIO a;
    if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {
        printf("Error: failed to initialize ALSA with %dHz, %dch\n", output_samplerate, info.nChans);
        free(file_data);
        close(fd);
        AACFreeDecoder(aac);
        return 1;
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
    ts.codec = sbr_enabled ? "AAC/SBR" : "AAC";
    ts.rate = output_samplerate;
    ts.channels = info.nChans;
    ts.device = dev;
    ts.use_time = 0; // AAC is decoded from a raw byte stream; show byte progress instead of time
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
            resync_attempts = 0; // Reset on successful decode
            AACGetLastFrameInfo(aac, &info);
            int samples_per_frame = sbr_enabled ? 2048 : 1024; // Samples per channel
            if (verbose) {
                printf("Frame: %d samples/channel, %d channels, %dHz\n", samples_per_frame, info.nChans, info.sampRateCore);
            }
            int frames = samples_per_frame; // Samples per channel
            if (flag & USE_CROSSTALK) {
                apply_crosstalk_cancellation(&xtc, sample_buf, frames, info.nChans, 0);
            }
            AUDIO_play(&a, (char*)sample_buf, frames);
            AUDIO_wait(&a, 100);
        } else {
            ts.note = "AAC decode error, stopping";
            tui_render(&ts);
            // For raw AAC (MP4), do not attempt ADTS-based resync—just stop on error.
            // If this is an ADTS file (.aac), you could add conditional resync logic here.
            break;
        }

        int k = key(&a, &ts);
        if (k == 'c') {
            flag ^= USE_CROSSTALK;
        } else if (k == KEY_LEFT || k == KEY_RIGHT) {
            ts.note = "seek not supported for AAC";
            tui_render(&ts);
        } else if (k) {
            break;
        }

        ts.cur_raw = (uint64_t)(stream_pos - file_data);
        ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
        ts.xtc_atten = xtc.attenuation;
        tui_render(&ts);
    }
    tui_close();

    AUDIO_close(&a);
    AACFreeDecoder(aac);
    free(file_data);
    close(fd);
    free_crosstalk_cancellation(&xtc);
    return 0;
}

// Extracts the parent directory portion of path into dir (size bytes).
// "music/rock/song.flac" -> "music/rock", "song.flac" -> "."
static void get_dirpart(const char *path, char *dir, size_t size)
{
	strncpy(dir, path, size - 1);
	dir[size - 1] = '\0';
	char *slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	else strcpy(dir, ".");
}

void play_dir(char *name, char *type, char *regexp, int flag)
{
	char path[1024], ext[10];
	int num, back=0;
	int format = flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;

	do {
		LS_LIST *ls = ls_dir(name, flag, &num);
		for (int i=0; i<num; i++) {
			char *e = findExt(ls[i].d_name);
			if (type) {
				if (!strstr(e, type)) {
					continue;
				}
			}
			// Interactive filter set with the 'f' key (see key()); re-read live
			// so toggling it mid-playlist takes effect from the next track.
			if (fmt_filter) {
				if (!strstr(e, fmt_filter)) {
					continue;
				}
			}
			if (regexp) {
				const char *error;
				//Reprog *p = regcomp(regexp, 0, &error);
				Reprog *p = regcomp(regexp, REG_ICASE, &error);
				if (!p) {
					fprintf(stderr, "regcomp: %s\n", error);
					return;
				}
				Resub m;
				if (regexec(p, ls[i].d_name, &m, 0)) {
					continue;
				}
			}

			track_index = i + 1;
			track_total = num;
			snprintf(path, 1024, "%s", ls[i].d_name);
			if (access(ls[i].d_name, F_OK)<0) {
				continue;
			}

			struct stat file_stat;
			if (stat(ls[i].d_name, &file_stat) < 0) {
				perror("stat");
				continue;
			}
			if (file_stat.st_size == 0) {
				printf("File size is 0: %s\n", ls[i].d_name);
				continue;
			}

			if (strstr(e, "flac")) {
				play_flac(path, format, flag);
			} else if (strstr(e, "mp3")) {
				play_mp3(path, format, flag);
			} else if (strstr(e, "mp4")) {
				play_aac(path, flag);
			} else if (strstr(e, "m4a")) {
				play_aac(path, flag);
			} else if (strstr(e, "ogg")) {
				play_ogg(path, flag);
			} else if (strstr(e, "wav")) {
				play_wav(path, format, flag);
			} else if (strstr(e, "wma")) {
				play_wma(path, flag);
			} else if (strstr(e, "dsf") || strstr(e, "dff")) {
				play_dsf(path, format, flag);
			} else {
				continue;
			}

			if (cmd=='\\' || cmd=='p' || cmd=='b') {
				i = back;
			} else if (cmd == 'd') {
				// Skip remaining tracks in the current directory
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
			if (cmd=='q' || cmd==0x1b) {
				break;
			}
			if (cmd != 'd') back = i-1;
		}
		free(ls);
	} while (loop_mode);
}

#include <sched.h>
void set_realtime_priority()
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
	for (int i=0; i<256; i++) {
		snprintf(buff, 255, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
		FILE *fp = fopen(buff, "w");
		if (!fp) {
			continue;
		}
		fprintf(fp, "%s", c);
		fclose(fp);
	}
}

void list_alsa_devices()
{
	snd_ctl_t *ctl;
	snd_ctl_card_info_t *info;
	snd_ctl_card_info_alloca(&info);
	int card = -1;
	printf("Available ALSA devices:\n");
	while (snd_card_next(&card) >= 0 && card >= 0) {
		char name[32];
		snprintf(name, sizeof(name), "hw:%d", card);
		if (snd_ctl_open(&ctl, name, 0) >= 0) {
			snd_ctl_card_info(ctl, info);
			printf("Card %d: %s\n", card, snd_ctl_card_info_get_name(info));
			snd_ctl_close(ctl);
		}
	}
}

void usage(FILE* fp, int argc, char** argv)
{
	fprintf(fp,
	        "Usage: %s [options] dir\n\n"
	        "Options:\n"
	        "-h                 Print this help message\n"
	        "-d <device name>   Specify ALSA device [e.g., default hw:0,0 plughw:0,0...]\n"
	        "-f                 Use 32-bit floating-point playback\n"
	        "-r                 Recursively search directories\n"
	        "-x                 Enable random playback\n"
	        "-s <regexp>        Search files with a regex\n"
	        "-t <ext type>      Specify file type (e.g., flac, mp3, wma, dsf, dff...)\n"
	        "-p                 Optimize for Linux platforms\n"
	        "-l                 Loop the directory playlist\n"
	        "-v                 Verbose mode\n"
	        "-V <volume>        Set ALSA mixer volume (0.0-1.0, default 1.0)\n"
	        "-c                 Enable crosstalk cancellation\n"
	        "-D                 Speaker distance for crosstalk cancellation\n"
	        "-T                 Enable test mode (sine wave: left, right, pan)\n"
	        "\n"
	        "During playback:\n"
	        "  Tab                Pause / resume\n"
	        "  Space              Pause / resume, releasing the ALSA device meanwhile\n"
	        "                     (so other apps can use the sound card)\n"
	        "  Left / Right       Seek -/+ %ds (FLAC, MP3, WAV, OGG)\n"
	        "  Up / Down          Volume +/- %.0f%%\n"
	        "  C                  Toggle crosstalk cancellation\n"
	        "  F                  Cycle playlist format filter (ALL/flac/mp3/m4a/ogg/wav/wma/dsf/dff)\n"
	        "  B / \\              Back to previous track\n"
	        "  d                  Skip to next directory\n"
	        "  Q / Esc            Quit\n"
	        "  (any other key)    Next track\n"
	        "\n",
	        argv[0], SEEK_SECONDS, VOLUME_STEP * 100.0f);
}

int main(int argc, char *argv[])
{
	int flag = 0;
	char *dir = ".";
	char *type = 0;
	char *regexp = 0;
	struct parg_state ps;
	int c;
	int clock = 0;

	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hd:frxs:t:pclvDTV")) != -1) {
		switch (c) {
		case 1:
			dir = (char*)ps.optarg;
			break;
		case 'd':
			dev = (char*)ps.optarg;
			break;
		case 'f':
			flag |= USE_FLOAT32;
			break;
		case 'r':
			flag |= LS_RECURSIVE;
			break;
		case 'x':
			flag |= LS_RANDOM;
			break;
		case 's':
			regexp = (char*)ps.optarg;
			printf("Search with '%s'.\n", regexp);
			break;
		case 't':
			type = (char*)ps.optarg;
			break;
		case 'p': {
			FILE *fp = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "w");
			fprintf(fp, "tsc");
			fclose(fp);
			set_realtime_priority();
			set_cpu("performance");
			clock = 1;
		}
		break;
		case 'c':
			flag |= USE_CROSSTALK;
			printf("Crosstalk cancellation enabled.\n");
			break;
		case 'T':
			flag |= USE_TEST_MODE;
			printf("Test mode enabled.\n");
			break;
		case 'l':
			loop_mode = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			volume = atof(ps.optarg);
			if (volume < 0.0f || volume > 1.0f) {
				volume = 1.0f;
			}
			break;
		case 'D':
			speaker_distance_m = atof(ps.optarg);
			break;
		case 'h':
			usage(stderr, argc, argv);
			list_alsa_devices();
			return 1;
		}
	}

	apply_alsa_volume(); // push -V's initial value (or the 1.0 default) to the mixer

	if (flag & USE_TEST_MODE) {
		int format = flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;
		play_test_mode(format, flag);
	} else {
		play_dir(dir, type, regexp, flag);
	}

	if (clock) {
		set_cpu("ondemand");
	}

	return 0;
}
