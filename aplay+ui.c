// ©2017-2026 Yuichiro Nakada
// MIT license
//
// aplay+ui.c -- aplay+engine.h playback engine with a luna-ui
// (OpenGL + CSS/HTML) GUI bridged on top via a second thread.
//
// clang -Os -o aplay+ui aplay+ui.c -lasound -lglfw -lGL -lpthread -ldl -lm            (dr_flac, default)
// clang -Os -DUSE_FOXEN_FLAC -o aplay+ui aplay+ui.c -lasound -lglfw -lGL -lpthread -ldl -lm   (foxen-flac)
//
// ./aplay+ui -rxfp -d hw:7,0 /Music/ -s '^(?!.*nstrumental).*$'

// ---- Step 1: declarations only (engine header included without implementation)
// This gives us the engine's playback-state type so the GUI can mirror it
// before the implementation functions are expanded in step 2.
#include "aplay+engine.h"

// ============================================================
// GUI bridge
//
// The engine runs on its own thread. The GUI observes playback state and
// injects commands; it does not read from or render to the terminal.
// ============================================================
#include <pthread.h>

static volatile int g_injected_key = 0;
static int g_wake_pipe[2] = { -1, -1 };

static void gui_bridge_init(void)
{
	if (pipe(g_wake_pipe) != 0) {
		g_wake_pipe[0] = g_wake_pipe[1] = -1;
	}
}

static int gui_take_injected_key(void)
{
	return __atomic_exchange_n(&g_injected_key, 0, __ATOMIC_SEQ_CST);
}

static void gui_inject_key(int key_code)
{
	__atomic_store_n(&g_injected_key, key_code, __ATOMIC_SEQ_CST);
	if (g_wake_pipe[1] >= 0) {
		char b = 1;
		ssize_t r = write(g_wake_pipe[1], &b, 1);
		(void)r;
	}
}

/* Discard wake bytes already queued (e.g. the same inject that entered
 * pause/stop). Without this, gui_wait_for_command() returns immediately. */
static void gui_drain_wake_pipe(void)
{
	if (g_wake_pipe[0] < 0) return;
	for (;;) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_wake_pipe[0], &fds);
		struct timeval tv = { 0, 0 };
		if (select(g_wake_pipe[0] + 1, &fds, NULL, NULL, &tv) <= 0) break;
		char buf[64];
		if (read(g_wake_pipe[0], buf, sizeof(buf)) <= 0) break;
	}
}

/* Re-queue a command without writing the wake pipe (playback is already
 * running / about to poll key() again). */
static void gui_requeue_key(int key_code)
{
	if (key_code)
		__atomic_store_n(&g_injected_key, key_code, __ATOMIC_SEQ_CST);
}

typedef struct {
	pthread_mutex_t lock;
	char filename[PATH_MAX];
	char dir[PATH_MAX];
	char codec[32];
	char note[128];
	char device[64];
	char format_filter[16];
	int rate, bits, channels;
	double cur, total;
	float volume;
	int paused;
	int loop_mode;
	int xtc_on;
	float xtc_atten;
	int dsd_on;
	int sr_on;
	int track_index, track_total;
	int playlist_first, playlist_count;
	char playlist_lines[13][256];
	unsigned version;
} GuiState;

static GuiState g_gui = { .lock = PTHREAD_MUTEX_INITIALIZER };
static volatile int g_gui_should_close = 0;
static char g_playlist_names[1024][256];
static int g_playlist_name_count = 0;
static int g_playlist_scroll_first = -1;
static int g_playlist_selected = -1;
/* One-based, like engine track_index; zero means no pending direct selection. */
static volatile int g_playlist_requested_track = 0;
static volatile int g_playlist_request_direction = 1;
static volatile int g_device_picker_mode = 0;
static int g_device_picker_scroll = 0;
static int g_device_picker_selected = -1;
static double g_playlist_last_click_time = 0.0;
static int g_playlist_last_click_item = -1;
/* Initial engine flag bitmask; XOR with g_flag_diff gives live DSD/SR/XTC. */
static int g_start_flag = 0;

static void gui_request_close(void)
{
	__atomic_store_n(&g_gui_should_close, 1, __ATOMIC_SEQ_CST);
}

static void gui_fill_device_picker_rows_locked(void)
{
	int count = aplay_device_count();
	int first = g_device_picker_scroll;
	if (first < 0) first = 0;
	if (first + 13 > count) first = count - 13;
	if (first < 0) first = 0;
	g_device_picker_scroll = first;
	g_gui.playlist_first = first;
	g_gui.playlist_count = count;
	for (int i = 0; i < 13; i++) {
		int item = first + i;
		const char *label = aplay_device_label(item);
		snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
			label ? label : "");
	}
}

static void gui_set_device_picker(int enabled)
{
	__atomic_store_n(&g_device_picker_mode, enabled ? 1 : 0, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&g_gui.lock);
	if (enabled) {
		aplay_refresh_devices();
		g_device_picker_scroll = 0;
		g_device_picker_selected = -1;
		/* Prefer scrolling so the current device is visible. */
		int n = aplay_device_count();
		for (int i = 0; i < n; i++) {
			const char *name = aplay_device_name(i);
			if (name && !strcmp(name, g_gui.device[0] ? g_gui.device : g_dev_storage)) {
				g_device_picker_selected = i;
				g_device_picker_scroll = i - 6;
				break;
			}
		}
		gui_fill_device_picker_rows_locked();
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "Select ALSA device");
	} else {
		g_device_picker_selected = -1;
		/* Restore playlist rows from the cached names. */
		int first = g_playlist_scroll_first >= 0 ? g_playlist_scroll_first : g_gui.playlist_first;
		int max_first = g_playlist_name_count > 13 ? g_playlist_name_count - 13 : 0;
		if (first > max_first) first = max_first;
		if (first < 0) first = 0;
		g_gui.playlist_first = first;
		g_gui.playlist_count = g_playlist_name_count;
		for (int i = 0; i < 13; i++) {
			int item = first + i;
			snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
				item < g_playlist_name_count ? g_playlist_names[item] : "");
		}
	}
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void gui_sync_from_playback_state(tui_state_t *ts)
{
	if (!ts) return;
	pthread_mutex_lock(&g_gui.lock);
	snprintf(g_gui.filename, sizeof(g_gui.filename), "%s", ts->filename ? ts->filename : "");
	snprintf(g_gui.dir, sizeof(g_gui.dir), "%s", ts->dir ? ts->dir : "");
	snprintf(g_gui.codec, sizeof(g_gui.codec), "%s", ts->codec ? ts->codec : "");
	snprintf(g_gui.note, sizeof(g_gui.note), "%s", ts->note ? ts->note : "");
	snprintf(g_gui.device, sizeof(g_gui.device), "%s", ts->device ? ts->device : "");
	snprintf(g_gui.format_filter, sizeof(g_gui.format_filter), "%s", ts->format_filter ? ts->format_filter : "ALL");
	g_gui.rate = ts->rate;
	g_gui.bits = ts->bits;
	g_gui.channels = ts->channels;
	g_gui.cur = ts->cur;
	g_gui.total = ts->total;
	g_gui.volume = ts->volume;
	g_gui.paused = ts->paused;
	g_gui.loop_mode = ts->loop_mode;
	g_gui.xtc_on = ts->xtc_on;
	g_gui.xtc_atten = ts->xtc_atten;
	g_gui.dsd_on = ((g_start_flag ^ g_flag_diff) & USE_DSD_ENCODE) ? 1 : 0;
	g_gui.sr_on = ((g_start_flag ^ g_flag_diff) & USE_SUPER_RES) ? 1 : 0;
	g_gui.track_index = ts->track_index;
	g_gui.track_total = ts->track_total;
	if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
		gui_fill_device_picker_rows_locked();
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "Select ALSA device");
	} else {
		int current = ts->track_index > 0 ? ts->track_index - 1 : 0;
		int first = g_playlist_scroll_first;
		if (first < 0) first = current - 6;
		if (first < 0) first = 0;
		if (first + 13 > g_playlist_name_count) first = g_playlist_name_count - 13;
		if (first < 0) first = 0;
		g_gui.playlist_first = first;
		g_gui.playlist_count = g_playlist_name_count;
		for (int i = 0; i < 13; i++) {
			int item = first + i;
			snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
				item < g_playlist_name_count ? g_playlist_names[item] : "");
		}
	}
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void gui_scroll_playlist(int delta)
{
	pthread_mutex_lock(&g_gui.lock);
	if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
		int count = aplay_device_count();
		int first = g_device_picker_scroll;
		int max_first = count > 13 ? count - 13 : 0;
		first += delta;
		if (first < 0) first = 0;
		if (first > max_first) first = max_first;
		g_device_picker_scroll = first;
		gui_fill_device_picker_rows_locked();
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	int first = g_playlist_scroll_first >= 0 ? g_playlist_scroll_first : g_gui.playlist_first;
	int max_first = g_playlist_name_count > 13 ? g_playlist_name_count - 13 : 0;
	first += delta;
	if (first < 0) first = 0;
	if (first > max_first) first = max_first;
	g_playlist_scroll_first = first;
	g_gui.playlist_first = first;
	for (int i = 0; i < 13; i++) {
		int item = first + i;
		snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
			item < g_playlist_name_count ? g_playlist_names[item] : "");
	}
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void gui_select_device_item(int item, int apply)
{
	int count = aplay_device_count();
	if (count <= 0) return;
	if (item < 0) item = 0;
	if (item >= count) item = count - 1;
	pthread_mutex_lock(&g_gui.lock);
	g_device_picker_selected = item;
	int first = g_device_picker_scroll;
	if (item < first) first = item;
	if (item >= first + 13) first = item - 12;
	int max_first = count > 13 ? count - 13 : 0;
	if (first > max_first) first = max_first;
	if (first < 0) first = 0;
	g_device_picker_scroll = first;
	gui_fill_device_picker_rows_locked();
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
	if (apply) {
		__atomic_store_n(&g_device_select_idx, item, __ATOMIC_SEQ_CST);
		gui_inject_key('D'); /* wake key() which applies g_device_select_idx */
		gui_set_device_picker(0);
	}
}

static void gui_select_playlist_item(int item, int play)
{
	if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
		gui_select_device_item(item, play);
		return;
	}
	pthread_mutex_lock(&g_gui.lock);
	if (item < 0) item = 0;
	if (item >= g_playlist_name_count) item = g_playlist_name_count - 1;
	if (item < 0) {
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	g_playlist_selected = item;
	int first = g_playlist_scroll_first >= 0 ? g_playlist_scroll_first : g_gui.playlist_first;
	if (item < first) first = item;
	if (item >= first + 13) first = item - 12;
	int max_first = g_playlist_name_count > 13 ? g_playlist_name_count - 13 : 0;
	if (first > max_first) first = max_first;
	if (first < 0) first = 0;
	g_playlist_scroll_first = first;
	g_gui.playlist_first = first;
	for (int i = 0; i < 13; i++) {
		int absolute = first + i;
		snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
			absolute < g_playlist_name_count ? g_playlist_names[absolute] : "");
	}
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);

	if (play) {
		pthread_mutex_lock(&g_gui.lock);
		int current = g_gui.track_index;
		pthread_mutex_unlock(&g_gui.lock);
		__atomic_store_n(&g_playlist_request_direction,
			current > 0 && item + 1 < current ? -1 : 1, __ATOMIC_SEQ_CST);
		__atomic_store_n(&g_playlist_requested_track, item + 1, __ATOMIC_SEQ_CST);
		/* Wake paused playback; normal playback sees the request in key(). */
		gui_inject_key('n');
	}
}

static void gui_scroll_playlist_to_fraction(double fraction)
{
	if (fraction < 0.0) fraction = 0.0;
	if (fraction > 1.0) fraction = 1.0;
	if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
		int count = aplay_device_count();
		int max_first = count > 13 ? count - 13 : 0;
		int target = (int)lround(fraction * max_first);
		pthread_mutex_lock(&g_gui.lock);
		int current = g_device_picker_scroll;
		pthread_mutex_unlock(&g_gui.lock);
		gui_scroll_playlist(target - current);
		return;
	}
	int max_first = g_playlist_name_count > 13 ? g_playlist_name_count - 13 : 0;
	int target = (int)lround(fraction * max_first);
	pthread_mutex_lock(&g_gui.lock);
	int current = g_playlist_scroll_first >= 0 ? g_playlist_scroll_first : g_gui.playlist_first;
	pthread_mutex_unlock(&g_gui.lock);
	gui_scroll_playlist(target - current);
}

// Engine playback paths publish their state through the old render hook.
// In this GUI build it only synchronizes state; no terminal UI is rendered.
static void gui_publish_state(tui_state_t *ts)
{
	gui_sync_from_playback_state(ts);
}
#define tui_render(ts) gui_publish_state(ts)
#define tui_open() ((void)0)
#define tui_close() ((void)0)

/* Capture the engine's actual, already sorted/shuffled listing.  Wrapping
 * ls_dir() here keeps the GUI playlist in exactly the same order as playback,
 * including -r and -x, without making a second directory scan.  Extra roots
 * added from the right-click menu are merged in on each (re)scan. */
#define AUI_MAX_PLAYLIST_ROOTS 32
static char g_playlist_roots[AUI_MAX_PLAYLIST_ROOTS][PATH_MAX];
static int g_playlist_root_count = 0;
static volatile int g_playlist_reload_req = 0;

static void aplay_playlist_request_reload(void)
{
	__atomic_store_n(&g_playlist_reload_req, 1, __ATOMIC_SEQ_CST);
}

static int aplay_playlist_reload_hook(void)
{
	return __atomic_exchange_n(&g_playlist_reload_req, 0, __ATOMIC_SEQ_CST);
}

static int aplay_playlist_reload_pending(void)
{
	return __atomic_load_n(&g_playlist_reload_req, __ATOMIC_SEQ_CST);
}

static int aplay_playlist_requested_track(void)
{
	return __atomic_load_n(&g_playlist_requested_track, __ATOMIC_SEQ_CST);
}

#define APLAY_PLAYLIST_RELOAD_HOOK() aplay_playlist_reload_hook()
#define APLAY_PLAYLIST_REQUESTED_TRACK() aplay_playlist_requested_track()

static void playlist_root_add(const char *dir)
{
	char abs[PATH_MAX];
	if (!dir || !dir[0]) return;
	if (!realpath(dir, abs))
		snprintf(abs, sizeof(abs), "%s", dir);
	for (int i = 0; i < g_playlist_root_count; i++) {
		if (!strcmp(g_playlist_roots[i], abs)) return;
	}
	if (g_playlist_root_count >= AUI_MAX_PLAYLIST_ROOTS) return;
	snprintf(g_playlist_roots[g_playlist_root_count], sizeof(g_playlist_roots[0]), "%s", abs);
	g_playlist_root_count++;
}

static int ls_list_has_path(const LS_LIST *ls, int n, const char *path)
{
	if (!ls || !path || !path[0]) return 0;
	for (int i = 0; i < n; i++) {
		if (ls[i].d_name[0] && !strcmp(ls[i].d_name, path)) return 1;
	}
	return 0;
}

static void gui_publish_playlist_names(const LS_LIST *list, int num)
{
	g_playlist_name_count = 0;
	g_playlist_selected = -1;
	g_playlist_scroll_first = -1;
	if (list && num > 0) {
		int count = num < 1024 ? num : 1024;
		for (int i = 0; i < count; i++) {
			if (!list[i].d_name[0]) continue;
			const char *base = strrchr(list[i].d_name, '/');
			base = base ? base + 1 : list[i].d_name;
			snprintf(g_playlist_names[g_playlist_name_count], sizeof(g_playlist_names[0]), "%s", base);
			g_playlist_name_count++;
		}
	}
	g_gui.playlist_first = 0;
	g_gui.playlist_count = g_playlist_name_count;
	for (int i = 0; i < 13; i++)
		snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
			i < g_playlist_name_count ? g_playlist_names[i] : "");
	g_gui.version++;
	fprintf(stderr, "aplay+ui: playlist contains %d item%s\n",
		g_playlist_name_count, g_playlist_name_count == 1 ? "" : "s");
}

static LS_LIST *gui_ls_dir(char *dir, int flag, int *num)
{
	playlist_root_add(dir);

	LS_LIST *list = NULL;
	if (g_playlist_root_count <= 1) {
		list = ls_dir(dir, flag, num);
	} else {
		int scan_flag = flag & ~LS_RANDOM;
		int total = 0, cap = 0;
		for (int r = 0; r < g_playlist_root_count; r++) {
			int part_n = 0;
			LS_LIST *part = ls_dir(g_playlist_roots[r], scan_flag, &part_n);
			if (!part) continue;
			for (int i = 0; i < part_n; i++) {
				if (!part[i].d_name[0]) continue;
				if (ls_list_has_path(list, total, part[i].d_name)) continue;
				if (total + 1 >= cap) {
					int ncap = cap ? cap * 2 : part_n + 32;
					LS_LIST *grown = (LS_LIST *)realloc(list, (size_t)ncap * sizeof(LS_LIST));
					if (!grown) break;
					list = grown;
					cap = ncap;
				}
				list[total++] = part[i];
			}
			free(part);
		}
		if (!list || total <= 0) {
			free(list);
			list = NULL;
			if (num) *num = 0;
		} else {
			if (flag & LS_RANDOM) {
				urandom_init();
				for (int i = total - 1; i > 0; i--) {
					uint32_t r = urandom_number();
					int a = (int)(r % (uint32_t)(i + 1));
					LS_LIST tmp = list[i];
					list[i] = list[a];
					list[a] = tmp;
				}
				urandom_end();
			} else {
				qsort(list, (size_t)total, sizeof(LS_LIST), ls_comp_func);
			}
			if (num) *num = total;
		}
	}

	pthread_mutex_lock(&g_gui.lock);
	gui_publish_playlist_names(list, num ? *num : 0);
	pthread_mutex_unlock(&g_gui.lock);
	return list;
}

// ---- Step 2: expand engine implementations (macro override already active)
#define ls_dir gui_ls_dir
#define APLAY_ENGINE_IMPLEMENTATION
#include "aplay+engine.h"
#undef ls_dir

// Pause/stop wait only for a *new* command from the GUI; stdin is ignored.
static void gui_wait_for_command(void)
{
	if (g_wake_pipe[0] < 0) return;
	gui_drain_wake_pipe();
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(g_wake_pipe[0], &fds);
	if (select(g_wake_pipe[0] + 1, &fds, NULL, NULL, NULL) > 0 &&
	    FD_ISSET(g_wake_pipe[0], &fds)) {
		char buf[16];
		(void)read(g_wake_pipe[0], buf, sizeof(buf));
	}
}

/* Soft pause (Space) or hard stop (Tab): block until the next GUI command.
 * Space/Tab only resume; other keys are re-queued for the next key() poll. */
static int gui_hold_playback(AUDIO *a, tui_state_t *ts, int hard_stop)
{
	if (hard_stop) {
		if (a) AUDIO_release(a);
	} else if (a && a->handle) {
		/* Many hw: devices reject pause; drop buffered samples instead. */
		if (snd_pcm_pause(a->handle, 1) < 0)
			snd_pcm_drop(a->handle);
	}
	if (ts) { ts->paused = 1; tui_render(ts); }

	gui_wait_for_command();
	int wake = gui_take_injected_key();
	cmd = 0;

	if (hard_stop) {
		if (a) {
			/* Device may still be busy — leave released and let the next
			 * outr_ensure_open / audio_wait_init retry. */
			if (AUDIO_reopen(a) == 0)
				apply_alsa_volume();
		}
	} else if (a && a->handle) {
		if (snd_pcm_pause(a->handle, 0) < 0)
			snd_pcm_prepare(a->handle);
		else
			snd_pcm_prepare(a->handle);
	}
	if (ts) { ts->paused = 0; tui_render(ts); }

	if (wake && wake != 0x20 && wake != 0x09)
		gui_requeue_key(wake);
	return 0;
}

// ---- Per-file: key() (GUI commands only; there is no terminal input path)
int key(AUDIO *a, tui_state_t *ts)
{
	int c = gui_take_injected_key();
	if (aplay_handle_device_keys(a, ts, c)) {
		cmd = 0;
		return 0;
	}
	int requested = __atomic_load_n(&g_playlist_requested_track, __ATOMIC_SEQ_CST);
	if (requested > 0 && track_index > 0) {
		/* Newly added folders are merged on reload.  If the click lands past
		 * the engine's current listing, abort this track and rebuild first. */
		if (aplay_playlist_reload_pending() || requested > track_total) {
			aplay_playlist_request_reload();
			cmd = 'A';
			return 'A';
		}
		int direction = __atomic_load_n(&g_playlist_request_direction, __ATOMIC_SEQ_CST);
		if (requested == track_index ||
		    (direction > 0 && track_index > requested) ||
		    (direction < 0 && track_index < requested)) {
			__atomic_store_n(&g_playlist_requested_track, 0, __ATOMIC_SEQ_CST);
			/* Consume the wake command used by gui_select_playlist_item(). */
			return 0;
		} else {
			/* play_dir() advances naturally for every command except previous. */
			cmd = direction < 0 ? 'b' : 'n';
			return cmd;
		}
	}
	if (!c) return 0;
	cmd = c;
	if (verbose) printf("gui:%x\n", c);

	if (c == 'f') {
		fmt_filter_idx = (fmt_filter_idx + 1) % fmt_cycle_n;
		fmt_filter = (char *)fmt_cycle[fmt_filter_idx];
		if (ts) {
			ts->format_filter = fmt_filter;
			ts->note = fmt_filter ? fmt_filter : "ALL";
			tui_render(ts);
		}
		return 0;
	}
	if (c == 'l') {
		loop_mode = !loop_mode;
		if (ts) {
			ts->loop_mode = loop_mode;
			ts->note = loop_mode ? "Playlist repeat on" : "Playlist repeat off";
			tui_render(ts);
		}
		return 0;
	}

	if (c == KEY_UP || c == KEY_DOWN) {
		volume += (c == KEY_UP ? VOLUME_STEP : -VOLUME_STEP);
		if (volume > 1.0f) volume = 1.0f;
		if (volume < 0.0f) volume = 0.0f;
		apply_alsa_volume();
		if (ts) { ts->volume = volume; tui_render(ts); }
		return 0;
	}

	if (c == '+' || c == '=' || c == '-' || c == '_') {
		xtc_attenuation += (c == '+' || c == '=') ? XTC_ATTEN_STEP : -XTC_ATTEN_STEP;
		if (xtc_attenuation > 1.0f) xtc_attenuation = 1.0f;
		if (xtc_attenuation < 0.0f) xtc_attenuation = 0.0f;
		if (ts) {
			ts->xtc_atten = xtc_attenuation;
			ts->note = "XTC attenuation";
			tui_render(ts);
		}
		return 0;
	}

	if (c == 0x20)
		return gui_hold_playback(a, ts, 0);
	if (c == 0x09)
		return gui_hold_playback(a, ts, 1);

	return c;
}

void usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
	        "Usage: %s [options] dir\n\n"
	        "A luna-ui player window opens automatically. Playback, seek, volume,\n"
	        "Crosstalk, DSD, super resolution, format filter, repeat, device, and quit are controlled\n"
	        "from the right-click menu (and keyboard shortcuts).\n"
	        "Add folder... appends a directory to the playlist.\n\n"
	        "Options:\n"
	        "-h                 Print this help message\n"
	        "-S <path>          Use a Winamp Classic .wsz file or extracted skin directory\n"
	        "--skin <path>      Same as -S\n"
	        "-R <dir>           Skin pack folder (.wsz files and/or skin directories).\n"
	        "                   When set, a random skin is applied on each track change\n"
	        "                   (toggle from the right-click menu)\n"
	        "--skins <dir>      Same as -R\n"
	        "-d <device name>   Specify ALSA device [default: first openable hw:N,M]\n"
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
	        "-D <meters>        Speaker distance for crosstalk cancellation\n"
	        "-T                 Enable test mode (sine wave: left, right, pan)\n"
	        "-e                 Start playback with real-time PCM->DSD64 (DoP)\n"
	        "                   output. Can be toggled live during playback with\n"
	        "                   the 'e' key (WAV/FLAC/MP3/OGG/WMA/AAC)\n"
	        "-o <path>          Also write the raw DSD bitstream generated whenever\n"
	        "                   DSD(DoP) output is active to <path> (optional; off\n"
	        "                   by default, playback is unaffected either way)\n"
	        "\n",
	        argv[0]);
}

// ============================================================
// luna-ui GUI
//
// A "now playing" panel driven entirely by reading g_gui and calling
// gui_inject_key(). It never touches ALSA, decoders, or the playlist.
// ============================================================
// stb_vorbis.h defines a single-letter macro `L` that clashes with `int L;`
// in stb_image.h (pulled in by luna-ui.h). Undefine it here before the include.
// Also rename luna-ui.h's utf8_decode to avoid conflict with tui.h's version.
#undef L
#define utf8_decode luna_utf8_decode
#define LUNA_UI_GLFW
#define LUNA_UI_IMPLEMENTATION
#include "luna-ui.h"
#include <GLFW/glfw3.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct {
	int enabled;
	int owns_extract_dir;
	char source[PATH_MAX];
	char work_dir[PATH_MAX];
	char root_dir[PATH_MAX];
	char main_png[PATH_MAX];
	char equalizer_png[PATH_MAX];
	char playlist_png[PATH_MAX];
	char prev_png[PATH_MAX];
	char play_png[PATH_MAX];
	char pause_png[PATH_MAX];
	char stop_png[PATH_MAX];
	char next_png[PATH_MAX];
	char eject_png[PATH_MAX];
	char prev_down_png[PATH_MAX];
	char play_down_png[PATH_MAX];
	char pause_down_png[PATH_MAX];
	char stop_down_png[PATH_MAX];
	char next_down_png[PATH_MAX];
	char eject_down_png[PATH_MAX];
	char playlist_normal[8];
	char playlist_current[8];
	char playlist_bg[8];
	char playlist_selected_bg[8];
} WinampSkin;

static const char *g_skin_arg = NULL;
static char g_skin_path[PATH_MAX];
static WinampSkin g_skin;

#define AUI_MAX_SKIN_PACK 512
static const char *g_skins_folder_arg = NULL;
static char g_skins_folder[PATH_MAX];
static char g_skin_pack[AUI_MAX_SKIN_PACK][PATH_MAX];
static int g_skin_pack_count = 0;
static int g_skin_pack_idx = -1;
static int g_skin_random = 0;
static int g_skin_last_track = -1;
static int g_pending_skin_reload = 0;
static char g_pending_skin_path[PATH_MAX];
static char g_skin_extract_dir[PATH_MAX];

static int path_is_dir(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int path_ends_with_ci(const char *path, const char *ext)
{
	size_t n, m;
	if (!path || !ext) return 0;
	n = strlen(path);
	m = strlen(ext);
	return n >= m && !strcasecmp(path + n - m, ext);
}

static void remove_skin_tree(const char *dir, int depth)
{
	if (!dir || strncmp(dir, "/tmp/aplay-winamp-", sizeof("/tmp/aplay-winamp-") - 1) != 0 || depth > 8) return;
	DIR *dp = opendir(dir);
	if (!dp) return;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path)) continue;
		struct stat st;
		if (lstat(path, &st) != 0) continue;
		if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) remove_skin_tree(path, depth + 1);
		else unlink(path);
	}
	closedir(dp);
	rmdir(dir);
}

static void cleanup_winamp_skin(void)
{
	if (g_skin.owns_extract_dir) remove_skin_tree(g_skin.work_dir, 0);
	memset(&g_skin, 0, sizeof(g_skin));
	g_skin_extract_dir[0] = '\0';
}

static int archive_entry_is_safe(const char *name)
{
	if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return 0;
	const char *p = name;
	while (*p) {
		const char *start = p;
		while (*p && *p != '/' && *p != '\\') p++;
		if ((p - start) == 2 && start[0] == '.' && start[1] == '.') return 0;
		if (*p) p++;
	}
	return 1;
}

static int wait_child_ok(pid_t pid)
{
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int validate_wsz_archive(const char *archive)
{
	int fd[2];
	if (pipe(fd) != 0) return 0;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fd[1], STDOUT_FILENO);
		close(fd[0]); close(fd[1]);
		execlp("unzip", "unzip", "-Z1", "--", archive, (char *)NULL);
		_exit(127);
	}
	close(fd[1]);
	if (pid < 0) { close(fd[0]); return 0; }
	FILE *fp = fdopen(fd[0], "r");
	char line[PATH_MAX];
	int safe = fp != NULL, count = 0;
	while (fp && fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!archive_entry_is_safe(line)) safe = 0;
		count++;
	}
	if (fp) fclose(fp); else close(fd[0]);
	return wait_child_ok(pid) && safe && count > 0;
}

static int extract_wsz_archive(const char *archive, const char *dest)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("unzip", "unzip", "-qq", "-o", "--", archive, "-d", dest, (char *)NULL);
		_exit(127);
	}
	return pid > 0 && wait_child_ok(pid);
}

static int find_skin_asset(const char *dir, const char *wanted, char *out, size_t out_n, int depth)
{
	if (depth > 4) return 0;
	DIR *dp = opendir(dir);
	if (!dp) return 0;
	struct dirent *de;
	int found = 0;
	while (!found && (de = readdir(dp)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path)) continue;
		struct stat st;
		if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) continue;
		if (S_ISREG(st.st_mode) && !strcasecmp(de->d_name, wanted)) {
			snprintf(out, out_n, "%s", path);
			found = 1;
		} else if (S_ISDIR(st.st_mode)) {
			found = find_skin_asset(path, wanted, out, out_n, depth + 1);
		}
	}
	closedir(dp);
	return found;
}

static int dir_has_skin_bmp(const char *dir)
{
	char tmp[PATH_MAX];
	return find_skin_asset(dir, "main.bmp", tmp, sizeof(tmp), 0) &&
	       find_skin_asset(dir, "cbuttons.bmp", tmp, sizeof(tmp), 0);
}

static int skin_pack_add(const char *path)
{
	if (!path || !path[0] || g_skin_pack_count >= AUI_MAX_SKIN_PACK) return 0;
	for (int i = 0; i < g_skin_pack_count; i++) {
		if (!strcmp(g_skin_pack[i], path)) return 1;
	}
	snprintf(g_skin_pack[g_skin_pack_count], sizeof(g_skin_pack[0]), "%s", path);
	g_skin_pack_count++;
	return 1;
}

static int scan_skins_folder(const char *folder)
{
	g_skin_pack_count = 0;
	g_skin_pack_idx = -1;
	if (!folder || !folder[0] || !path_is_dir(folder)) return 0;
	DIR *dp = opendir(folder);
	if (!dp) return 0;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", folder, de->d_name) >= (int)sizeof(path))
			continue;
		struct stat st;
		if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) continue;
		if (S_ISREG(st.st_mode) && path_ends_with_ci(de->d_name, ".wsz"))
			skin_pack_add(path);
		else if (S_ISDIR(st.st_mode) && dir_has_skin_bmp(path))
			skin_pack_add(path);
	}
	closedir(dp);
	/* Folder itself may be a single extracted skin. */
	if (g_skin_pack_count == 0 && dir_has_skin_bmp(folder))
		skin_pack_add(folder);
	return g_skin_pack_count;
}

static const char *skin_pack_pick(int prefer_different)
{
	if (g_skin_pack_count <= 0) return NULL;
	if (g_skin_pack_count == 1) {
		g_skin_pack_idx = 0;
		return g_skin_pack[0];
	}
	int idx;
	if (g_skin_random) {
		idx = (int)(xoroshiro128plus() % (uint64_t)g_skin_pack_count);
		if (prefer_different && idx == g_skin_pack_idx)
			idx = (idx + 1) % g_skin_pack_count;
	} else {
		idx = (g_skin_pack_idx + 1 + g_skin_pack_count) % g_skin_pack_count;
	}
	g_skin_pack_idx = idx;
	return g_skin_pack[idx];
}

static void gui_invalidate_texture_path(const char *path)
{
	if (!path || !path[0] || g_tex_count <= 0) return;
	for (int i = 0; i < g_tex_count; i++) {
		if (strcmp(g_tex_cache[i].path, path) != 0) continue;
		if (g_tex_cache[i].tex && glfwGetCurrentContext())
			glDeleteTextures(1, &g_tex_cache[i].tex);
		g_tex_cache[i] = g_tex_cache[g_tex_count - 1];
		memset(&g_tex_cache[g_tex_count - 1], 0, sizeof(g_tex_cache[0]));
		g_tex_count--;
		return;
	}
}

static void gui_invalidate_skin_textures(void)
{
	const char *paths[] = {
		g_skin.main_png, g_skin.equalizer_png, g_skin.playlist_png,
		g_skin.prev_png, g_skin.play_png, g_skin.pause_png, g_skin.stop_png,
		g_skin.next_png, g_skin.eject_png,
		g_skin.prev_down_png, g_skin.play_down_png, g_skin.pause_down_png,
		g_skin.stop_down_png, g_skin.next_down_png, g_skin.eject_down_png
	};
	for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
		gui_invalidate_texture_path(paths[i]);
}

static int write_skin_crop(const unsigned char *src, int sw, int sh,
	int x, int y, int w, int h, const char *path)
{
	if (!src || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > sw || y + h > sh) return 0;
	unsigned char *crop = malloc((size_t)w * (size_t)h * 4);
	if (!crop) return 0;
	for (int row = 0; row < h; row++) {
		memcpy(crop + (size_t)row * w * 4,
		       src + ((size_t)(y + row) * sw + x) * 4,
		       (size_t)w * 4);
	}
	int ok = stbi_write_png(path, w, h, 4, crop, w * 4);
	free(crop);
	return ok;
}

static int create_skin_workspace(const char *source_name)
{
	/* Invalidate while g_skin.*_png paths are still valid.  The GL texture
	 * cache keys on path and we rewrite those PNGs in-place on every reload,
	 * so skipping this leaves the previous skin's pixels on screen. */
	gui_invalidate_skin_textures();

	int reuse = g_skin.work_dir[0] && path_is_dir(g_skin.work_dir);
	char keep_work[PATH_MAX];
	int keep_owns = 0;
	if (reuse) {
		snprintf(keep_work, sizeof(keep_work), "%s", g_skin.work_dir);
		keep_owns = g_skin.owns_extract_dir;
		/* Drop extract tree so the next .wsz does not mix with leftovers. */
		if (g_skin_extract_dir[0] && path_is_dir(g_skin_extract_dir))
			remove_skin_tree(g_skin_extract_dir, 0);
	}
	memset(&g_skin, 0, sizeof(g_skin));
	snprintf(g_skin.source, sizeof(g_skin.source), "%s", source_name ? source_name : "aplay+ Ember");
	if (reuse) {
		snprintf(g_skin.work_dir, sizeof(g_skin.work_dir), "%s", keep_work);
		g_skin.owns_extract_dir = keep_owns;
	} else {
		snprintf(g_skin.work_dir, sizeof(g_skin.work_dir), "/tmp/aplay-winamp-%ld-XXXXXX", (long)getpid());
		if (!mkdtemp(g_skin.work_dir)) {
			fprintf(stderr, "aplay+ui: cannot create temporary skin directory: %s\n", strerror(errno));
			return 0;
		}
		g_skin.owns_extract_dir = 1;
	}
	snprintf(g_skin_extract_dir, sizeof(g_skin_extract_dir), "%s/extract", g_skin.work_dir);
	return 1;
}

static void set_skin_output_paths(void)
{
	snprintf(g_skin.main_png, sizeof(g_skin.main_png), "%s/main.png", g_skin.work_dir);
	snprintf(g_skin.equalizer_png, sizeof(g_skin.equalizer_png), "%s/equalizer.png", g_skin.work_dir);
	snprintf(g_skin.playlist_png, sizeof(g_skin.playlist_png), "%s/playlist.png", g_skin.work_dir);
	snprintf(g_skin.prev_png, sizeof(g_skin.prev_png), "%s/prev.png", g_skin.work_dir);
	snprintf(g_skin.play_png, sizeof(g_skin.play_png), "%s/play.png", g_skin.work_dir);
	snprintf(g_skin.pause_png, sizeof(g_skin.pause_png), "%s/pause.png", g_skin.work_dir);
	snprintf(g_skin.stop_png, sizeof(g_skin.stop_png), "%s/stop.png", g_skin.work_dir);
	snprintf(g_skin.next_png, sizeof(g_skin.next_png), "%s/next.png", g_skin.work_dir);
	snprintf(g_skin.eject_png, sizeof(g_skin.eject_png), "%s/eject.png", g_skin.work_dir);
	snprintf(g_skin.prev_down_png, sizeof(g_skin.prev_down_png), "%s/prev-down.png", g_skin.work_dir);
	snprintf(g_skin.play_down_png, sizeof(g_skin.play_down_png), "%s/play-down.png", g_skin.work_dir);
	snprintf(g_skin.pause_down_png, sizeof(g_skin.pause_down_png), "%s/pause-down.png", g_skin.work_dir);
	snprintf(g_skin.stop_down_png, sizeof(g_skin.stop_down_png), "%s/stop-down.png", g_skin.work_dir);
	snprintf(g_skin.next_down_png, sizeof(g_skin.next_down_png), "%s/next-down.png", g_skin.work_dir);
	snprintf(g_skin.eject_down_png, sizeof(g_skin.eject_down_png), "%s/eject-down.png", g_skin.work_dir);
	snprintf(g_skin.playlist_normal, sizeof(g_skin.playlist_normal), "#E4D6C5");
	snprintf(g_skin.playlist_current, sizeof(g_skin.playlist_current), "#FFE6B8");
	snprintf(g_skin.playlist_bg, sizeof(g_skin.playlist_bg), "#14110F");
	snprintf(g_skin.playlist_selected_bg, sizeof(g_skin.playlist_selected_bg), "#5C3A22");
}

static int write_skin_sprites(const unsigned char *main_rgba, int mw, int mh,
	const unsigned char *buttons_rgba, int bw, int bh)
{
	set_skin_output_paths();
	int down_y = bh >= 36 ? 18 : 0;
	int ok = write_skin_crop(main_rgba, mw, mh, 0, 0, 275, 116, g_skin.main_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 0, 0, 23, 18, g_skin.prev_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 23, 0, 23, 18, g_skin.play_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 46, 0, 23, 18, g_skin.pause_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 69, 0, 23, 18, g_skin.stop_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 92, 0, 22, 18, g_skin.next_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 114, 0, 22, 18, g_skin.eject_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 0, down_y, 23, 18, g_skin.prev_down_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 23, down_y, 23, 18, g_skin.play_down_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 46, down_y, 23, 18, g_skin.pause_down_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 69, down_y, 23, 18, g_skin.stop_down_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 92, down_y, 22, 18, g_skin.next_down_png) &&
	         write_skin_crop(buttons_rgba, bw, bh, 114, down_y, 22, 18, g_skin.eject_down_png);
	g_skin.enabled = ok;
	return ok;
}

static void skin_pixel(unsigned char *p, int w, int h, int x, int y,
	unsigned char r, unsigned char g, unsigned char b)
{
	if (!p || x < 0 || y < 0 || x >= w || y >= h) return;
	unsigned char *d = p + ((size_t)y * w + x) * 4;
	d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
}

static void skin_rect(unsigned char *p, int pw, int ph, int x, int y, int w, int h,
	unsigned char r, unsigned char g, unsigned char b)
{
	for (int yy = y; yy < y + h; yy++)
		for (int xx = x; xx < x + w; xx++) skin_pixel(p, pw, ph, xx, yy, r, g, b);
}

static void skin_line(unsigned char *p, int pw, int ph, int x0, int y0, int x1, int y1,
	unsigned char r, unsigned char g, unsigned char b)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		skin_pixel(p, pw, ph, x0, y0, r, g, b);
		if (x0 == x1 && y0 == y1) break;
		int e2 = err * 2;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

static void skin_bevel(unsigned char *p, int pw, int ph, int x, int y, int w, int h, int pressed)
{
	unsigned char hi = pressed ? 58 : 118;
	unsigned char lo = pressed ? 28 : 22;
	skin_rect(p, pw, ph, x, y, w, h,
		pressed ? 42 : 58, pressed ? 34 : 46, pressed ? 28 : 38);
	skin_line(p, pw, ph, x, y, x + w - 1, y, hi, (unsigned char)(hi - 18), (unsigned char)(hi - 36));
	skin_line(p, pw, ph, x, y, x, y + h - 1, hi, (unsigned char)(hi - 18), (unsigned char)(hi - 36));
	skin_line(p, pw, ph, x, y + h - 1, x + w - 1, y + h - 1, lo, lo + 4, lo + 6);
	skin_line(p, pw, ph, x + w - 1, y, x + w - 1, y + h - 1, lo, lo + 4, lo + 6);
}

static void skin_blit(unsigned char *dst, int dw, int dh,
	const unsigned char *src, int sw, int sh,
	int sx, int sy, int w, int h, int dx, int dy)
{
	if (!dst || !src) return;
	for (int y = 0; y < h; y++) {
		if (sy + y < 0 || sy + y >= sh || dy + y < 0 || dy + y >= dh) continue;
		for (int x = 0; x < w; x++) {
			if (sx + x < 0 || sx + x >= sw || dx + x < 0 || dx + x >= dw) continue;
			memcpy(dst + ((size_t)(dy + y) * dw + dx + x) * 4,
			       src + ((size_t)(sy + y) * sw + sx + x) * 4, 4);
		}
	}
}

static void skin_hex_rgb(const char *hex, unsigned char *r, unsigned char *g, unsigned char *b)
{
	unsigned value = 0;
	if (!hex || hex[0] != '#' || sscanf(hex + 1, "%06x", &value) != 1) value = 0;
	*r = (value >> 16) & 255; *g = (value >> 8) & 255; *b = value & 255;
}

static void read_playlist_colors(const char *path)
{
	if (!path || !path[0]) return;
	FILE *fp = fopen(path, "r");
	if (!fp) return;
	char line[128], key[32], value[32];
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, " %31[^=]=%31s", key, value) != 2 || value[0] != '#') continue;
		value[7] = '\0';
		if (!strcasecmp(key, "Normal")) snprintf(g_skin.playlist_normal, sizeof(g_skin.playlist_normal), "%s", value);
		else if (!strcasecmp(key, "Current")) snprintf(g_skin.playlist_current, sizeof(g_skin.playlist_current), "%s", value);
		else if (!strcasecmp(key, "NormalBG")) snprintf(g_skin.playlist_bg, sizeof(g_skin.playlist_bg), "%s", value);
		else if (!strcasecmp(key, "SelectedBG")) snprintf(g_skin.playlist_selected_bg, sizeof(g_skin.playlist_selected_bg), "%s", value);
	}
	fclose(fp);
}

/* Build the classic 275x232 Playlist Editor from PLEDIT.BMP's stretchable
 * chrome pieces.  The standard-width bottom is exactly its 125px left and
 * 150px right sprites; the middle side rails repeat vertically. */
static int write_skin_playlist(const unsigned char *pledit, int pw, int ph)
{
	const int w = 275, h = 232, top_h = 20, bottom_h = 38, bottom_y = 194;
	unsigned char *out = calloc((size_t)w * h, 4);
	if (!out) return 0;
	unsigned char r, g, b;
	skin_hex_rgb(g_skin.playlist_bg, &r, &g, &b);
	skin_rect(out, w, h, 0, 0, w, h, r, g, b);

	if (pledit && pw >= 276 && ph >= 110) {
		for (int x = 0; x < w; x += 25)
			skin_blit(out, w, h, pledit, pw, ph, 127, 0, 25, top_h, x, 0);
		skin_blit(out, w, h, pledit, pw, ph, 0, 0, 25, top_h, 0, 0);
		skin_blit(out, w, h, pledit, pw, ph, 26, 0, 100, top_h, 87, 0);
		skin_blit(out, w, h, pledit, pw, ph, 153, 0, 25, top_h, 250, 0);
		for (int y = top_h; y < bottom_y; y += 29) {
			skin_blit(out, w, h, pledit, pw, ph, 0, 42, 12, 29, 0, y);
			skin_blit(out, w, h, pledit, pw, ph, 31, 42, 20, 29, 255, y);
		}
		skin_blit(out, w, h, pledit, pw, ph, 0, 72, 125, bottom_h, 0, bottom_y);
		skin_blit(out, w, h, pledit, pw, ph, 126, 72, 150, bottom_h, 125, bottom_y);
	} else {
		skin_bevel(out, w, h, 0, 0, w, h, 0);
		skin_rect(out, w, h, 3, 3, w - 6, 17, 18, 15, 12);
		skin_rect(out, w, h, 3, bottom_y, w - 6, bottom_h - 3, 22, 19, 16);
	}
	int ok = stbi_write_png(g_skin.playlist_png, w, h, 4, out, w * 4);
	free(out);
	return ok;
}

static int write_skin_equalizer(const unsigned char *eq, int ew, int eh)
{
	if (eq && ew >= 275 && eh >= 116)
		return write_skin_crop(eq, ew, eh, 0, 0, 275, 116, g_skin.equalizer_png);
	unsigned char *out = calloc((size_t)275 * 116, 4);
	if (!out) return 0;
	skin_rect(out, 275, 116, 0, 0, 275, 116, 28, 24, 20);
	for (int y = 0; y < 116; y += 3)
		skin_line(out, 275, 116, 0, y, 274, y, 32, 27, 22);
	skin_bevel(out, 275, 116, 0, 0, 275, 116, 0);
	skin_rect(out, 275, 116, 3, 3, 269, 11, 18, 15, 12);
	skin_rect(out, 275, 116, 3, 13, 269, 1, 232, 168, 104);
	skin_rect(out, 275, 116, 8, 20, 258, 86, 10, 14, 12);
	for (int i = 0; i < 10; i++) {
		int x = 78 + i * 18;
		skin_line(out, 275, 116, x, 38, x, 96, 58, 46, 34);
	}
	int ok = stbi_write_png(g_skin.equalizer_png, 275, 116, 4, out, 275 * 4);
	free(out);
	return ok;
}

static void draw_transport_icon(unsigned char *p, int x, int y, int kind, int pressed)
{
	int ox = pressed ? 1 : 0, oy = pressed ? 1 : 0;
	unsigned char r = 232, g = 168, b = 104;
	x += ox; y += oy;
	if (kind == 0) {
		skin_line(p, 136, 36, x + 7, y + 5, x + 7, y + 12, r, g, b);
		skin_line(p, 136, 36, x + 8, y + 8, x + 14, y + 4, r, g, b);
		skin_line(p, 136, 36, x + 8, y + 8, x + 14, y + 12, r, g, b);
	} else if (kind == 1) {
		for (int i = 0; i < 7; i++) skin_line(p, 136, 36, x + 8 + i, y + 5 + i / 2, x + 8 + i, y + 12 - i / 2, r, g, b);
	} else if (kind == 2) {
		skin_rect(p, 136, 36, x + 8, y + 5, 3, 8, r, g, b);
		skin_rect(p, 136, 36, x + 14, y + 5, 3, 8, r, g, b);
	} else {
		skin_line(p, 136, 36, x + 8, y + 4, x + 14, y + 8, r, g, b);
		skin_line(p, 136, 36, x + 8, y + 12, x + 14, y + 8, r, g, b);
		skin_line(p, 136, 36, x + 15, y + 5, x + 15, y + 12, r, g, b);
	}
}

/* Original aplay+ Ember skin. Warm charcoal + copper — MIT, no Winamp art. */
static int prepare_builtin_skin(void)
{
	if (!create_skin_workspace("aplay+ Ember (MIT)")) return 0;
	unsigned char *main_rgba = calloc((size_t)275 * 116, 4);
	unsigned char *buttons_rgba = calloc((size_t)136 * 36, 4);
	if (!main_rgba || !buttons_rgba) { free(main_rgba); free(buttons_rgba); return 0; }

	/* Chassis */
	skin_rect(main_rgba, 275, 116, 0, 0, 275, 116, 28, 24, 20);
	for (int y = 0; y < 116; y += 3)
		skin_line(main_rgba, 275, 116, 0, y, 274, y, 32, 27, 22);
	skin_bevel(main_rgba, 275, 116, 0, 0, 275, 116, 0);
	/* Title bar */
	skin_rect(main_rgba, 275, 116, 3, 3, 269, 12, 18, 15, 12);
	skin_rect(main_rgba, 275, 116, 3, 14, 269, 1, 232, 168, 104);
	/* Tiny amber spectrum ticks */
	for (int i = 0; i < 5; i++)
		skin_rect(main_rgba, 275, 116, 8 + i * 4, 11 - i, 2, 2 + i, 232, 168, 104);
	/* Time / LCD wells */
	skin_bevel(main_rgba, 275, 116, 10, 18, 89, 48, 1);
	skin_rect(main_rgba, 275, 116, 13, 21, 83, 42, 10, 14, 12);
	skin_bevel(main_rgba, 275, 116, 103, 18, 164, 48, 1);
	skin_rect(main_rgba, 275, 116, 106, 21, 158, 42, 10, 14, 12);
	/* Progress + volume recesses */
	skin_bevel(main_rgba, 275, 116, 14, 69, 252, 12, 1);
	skin_rect(main_rgba, 275, 116, 17, 72, 246, 6, 12, 10, 8);
	skin_bevel(main_rgba, 275, 116, 103, 55, 72, 9, 1);
	skin_rect(main_rgba, 275, 116, 106, 58, 66, 3, 12, 10, 8);
	/* Transport deck */
	skin_rect(main_rgba, 275, 116, 3, 84, 269, 29, 22, 19, 16);
	skin_line(main_rgba, 275, 116, 3, 84, 271, 84, 92, 72, 48);

	for (int row = 0; row < 2; row++) {
		int y = row * 18;
		const int xs[6] = {0, 23, 46, 69, 92, 114};
		const int ws[6] = {23, 23, 23, 23, 22, 22};
		for (int i = 0; i < 6; i++) skin_bevel(buttons_rgba, 136, 36, xs[i], y, ws[i], 18, row);
		draw_transport_icon(buttons_rgba, 0, y, 0, row);
		draw_transport_icon(buttons_rgba, 23, y, 1, row);
		draw_transport_icon(buttons_rgba, 46, y, 2, row);
		draw_transport_icon(buttons_rgba, 92, y, 3, row);
		/* Stop square + eject triangle drawn as rects */
		skin_rect(buttons_rgba, 136, 36, 69 + 8 + row, 5 + row, 7, 7, 232, 168, 104);
		skin_line(buttons_rgba, 136, 36, 114 + 7 + row, 5 + row, 114 + 15 + row, 9 + row, 232, 168, 104);
		skin_line(buttons_rgba, 136, 36, 114 + 7 + row, 13 + row, 114 + 15 + row, 9 + row, 232, 168, 104);
		skin_line(buttons_rgba, 136, 36, 114 + 7 + row, 5 + row, 114 + 7 + row, 13 + row, 232, 168, 104);
	}

	int ok = write_skin_sprites(main_rgba, 275, 116, buttons_rgba, 136, 36) &&
	         write_skin_equalizer(NULL, 0, 0) &&
	         write_skin_playlist(NULL, 0, 0);
	free(main_rgba);
	free(buttons_rgba);
	if (!ok) fprintf(stderr, "aplay+ui: failed to generate the built-in MIT skin\n");
	return ok;
}

static int prepare_winamp_skin(const char *source)
{
	if (!source || !source[0]) return 0;
	struct stat source_st;
	if (stat(source, &source_st) != 0) {
		fprintf(stderr, "aplay+ui: skin path not found: %s (%s)\n", source, strerror(errno));
		return 0;
	}
	/* Pack folder: directory with multiple skins, not a single MAIN.BMP skin. */
	if (S_ISDIR(source_st.st_mode) && !dir_has_skin_bmp(source)) {
		if (scan_skins_folder(source) > 0) {
			snprintf(g_skins_folder, sizeof(g_skins_folder), "%s", source);
			g_skin_random = 1;
			const char *pick = skin_pack_pick(0);
			if (pick) return prepare_winamp_skin(pick);
		}
		fprintf(stderr, "aplay+ui: no Winamp skins found in folder: %s\n", source);
		return 0;
	}
	if (!create_skin_workspace(source)) return 0;

	if (path_is_dir(source)) {
		snprintf(g_skin.root_dir, sizeof(g_skin.root_dir), "%s", source);
	} else {
		if (!validate_wsz_archive(source)) {
			fprintf(stderr, "aplay+ui: invalid/unsafe .wsz archive or 'unzip' is unavailable: %s\n", source);
			return 0;
		}
		if (mkdir(g_skin_extract_dir, 0700) != 0 && errno != EEXIST) {
			fprintf(stderr, "aplay+ui: cannot create extract dir: %s\n", strerror(errno));
			return 0;
		}
		if (!extract_wsz_archive(source, g_skin_extract_dir)) {
			fprintf(stderr, "aplay+ui: failed to extract Winamp skin: %s\n", source);
			return 0;
		}
		snprintf(g_skin.root_dir, sizeof(g_skin.root_dir), "%s", g_skin_extract_dir);
	}

	char main_bmp[PATH_MAX], buttons_bmp[PATH_MAX];
	if (!find_skin_asset(g_skin.root_dir, "main.bmp", main_bmp, sizeof(main_bmp), 0) ||
	    !find_skin_asset(g_skin.root_dir, "cbuttons.bmp", buttons_bmp, sizeof(buttons_bmp), 0)) {
		fprintf(stderr, "aplay+ui: skin requires MAIN.BMP and CBUTTONS.BMP: %s\n", source);
		return 0;
	}

	stbi_set_flip_vertically_on_load(0);
	int mw, mh, mc, bw, bh, bc;
	unsigned char *main_rgba = stbi_load(main_bmp, &mw, &mh, &mc, 4);
	unsigned char *buttons_rgba = stbi_load(buttons_bmp, &bw, &bh, &bc, 4);
	if (!main_rgba || !buttons_rgba || mw < 275 || mh < 116 || bw < 114 || bh < 18) {
		fprintf(stderr, "aplay+ui: unsupported Winamp skin bitmap dimensions: %s\n", source);
		if (main_rgba) stbi_image_free(main_rgba);
		if (buttons_rgba) stbi_image_free(buttons_rgba);
		return 0;
	}

	int ok = write_skin_sprites(main_rgba, mw, mh, buttons_rgba, bw, bh);
	char eqmain_bmp[PATH_MAX] = "";
	unsigned char *eqmain_rgba = NULL;
	int ew = 0, eh = 0, ec = 0;
	if (find_skin_asset(g_skin.root_dir, "eqmain.bmp", eqmain_bmp, sizeof(eqmain_bmp), 0))
		eqmain_rgba = stbi_load(eqmain_bmp, &ew, &eh, &ec, 4);
	ok = ok && write_skin_equalizer(eqmain_rgba, ew, eh);
	char pledit_bmp[PATH_MAX] = "", pledit_txt[PATH_MAX] = "";
	unsigned char *pledit_rgba = NULL;
	int pw = 0, ph = 0, pc = 0;
	if (find_skin_asset(g_skin.root_dir, "pledit.txt", pledit_txt, sizeof(pledit_txt), 0))
		read_playlist_colors(pledit_txt);
	if (find_skin_asset(g_skin.root_dir, "pledit.bmp", pledit_bmp, sizeof(pledit_bmp), 0))
		pledit_rgba = stbi_load(pledit_bmp, &pw, &ph, &pc, 4);
	ok = ok && write_skin_playlist(pledit_rgba, pw, ph);
	stbi_image_free(main_rgba);
	stbi_image_free(buttons_rgba);
	if (eqmain_rgba) stbi_image_free(eqmain_rgba);
	if (pledit_rgba) stbi_image_free(pledit_rgba);
	g_skin.enabled = ok;
	if (!ok) fprintf(stderr, "aplay+ui: failed to prepare Winamp skin sprites: %s\n", source);
	return ok;
}

static const char *WINAMP_HTML =
    "<div id=\"app\" class=\"winamp-app ts-1\">"
    "  <div id=\"surface-main\" class=\"skin-surface skin-main\">"
    "    <span id=\"badge-state\" class=\"skin-state\">PLAYING</span>"
    "    <span id=\"track-count\" class=\"skin-track-count\"></span>"
    "    <span id=\"time-cur\" class=\"skin-time\">0:00</span>"
    "    <span id=\"time-total\" class=\"skin-time-total\">0:00</span>"
    "    <div id=\"title\" class=\"skin-title\">No track loaded</div>"
    "    <div id=\"subtitle\" class=\"skin-subtitle\">-</div>"
    "    <span id=\"badge-codec\" class=\"skin-codec\">-</span>"
    "    <span id=\"badge-rate\" class=\"skin-rate\">-</span>"
    "    <div class=\"skin-progress-track\"><div id=\"progress-fill\" class=\"skin-progress-fill\"></div></div>"
    "    <div class=\"skin-volume-track\"><div id=\"vol-fill\" class=\"skin-volume-fill\"></div></div>"
    "    <span id=\"vol-value\" class=\"skin-volume-value\">100%</span>"
    "    <button class=\"skin-btn skin-prev\" aria-label=\"Previous track\" onclick=\"onPrev()\"></button>"
    "    <button class=\"skin-btn skin-play\" aria-label=\"Play\" onclick=\"onPlayPause()\"></button>"
    "    <button id=\"playbtn\" class=\"skin-btn skin-pause\" aria-label=\"Pause\" onclick=\"onPlayPause()\"></button>"
    "    <button class=\"skin-btn skin-stop\" aria-label=\"Stop\" onclick=\"onStop()\"></button>"
    "    <button class=\"skin-btn skin-next\" aria-label=\"Next track\" onclick=\"onNext()\"></button>"
    "    <button class=\"skin-btn skin-eject\" aria-label=\"Cycle format filter\" onclick=\"onFmt()\"></button>"
    "    <button class=\"skin-panel-toggle skin-toggle-eq\" aria-label=\"Show or hide equalizer\" onclick=\"onToggleEq()\">EQ</button>"
    "    <button class=\"skin-panel-toggle skin-toggle-pl\" aria-label=\"Show or hide playlist\" onclick=\"onTogglePlaylist()\">PL</button>"
    "    <button class=\"skin-window-close\" aria-label=\"Quit player\" onclick=\"onQuit()\"></button>"
    "  </div>"
    "  <div id=\"surface-equalizer\" class=\"skin-surface skin-equalizer\">"
    "    <button class=\"skin-window-close\" aria-label=\"Hide equalizer\" onclick=\"onHideEq()\"></button>"
    "    <span class=\"eq-status\">EQ DISPLAY</span>"
    "    <div class=\"eq-analyzer\"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div>"
    "    <div class=\"eq-sliders\"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div>"
    "  </div>"
    "  <div id=\"surface-playlist\" class=\"skin-surface skin-playlist\">"
    "    <button class=\"skin-window-close\" aria-label=\"Hide playlist\" onclick=\"onHidePlaylist()\"></button>"
    "    <div class=\"skin-list\">"
    "      <div id=\"pl0\" class=\"skin-list-row\"></div><div id=\"pl1\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl2\" class=\"skin-list-row\"></div><div id=\"pl3\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl4\" class=\"skin-list-row\"></div><div id=\"pl5\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl6\" class=\"skin-list-row\"></div><div id=\"pl7\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl8\" class=\"skin-list-row\"></div><div id=\"pl9\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl10\" class=\"skin-list-row\"></div><div id=\"pl11\" class=\"skin-list-row\"></div>"
    "      <div id=\"pl12\" class=\"skin-list-row\"></div>"
    "    </div>"
    "    <div class=\"skin-scroll-track\"><div id=\"playlist-thumb\" class=\"skin-scroll-thumb\"></div></div>"
    "    <span id=\"device\" class=\"skin-device\" onclick=\"onDevicePicker()\">-</span><span id=\"note\" class=\"skin-note\">Ready</span>"
    "  </div>"
    "  <div id=\"surface-menu\" class=\"ctx-menu\">"
    "    <button id=\"mi-play\" class=\"ctx-item\" onclick=\"onMenuPlayPause()\"><span class=\"ctx-label\">Play / Pause</span><span class=\"ctx-kbd\">Space</span></button>"
    "    <button id=\"mi-stop\" class=\"ctx-item\" onclick=\"onMenuStop()\"><span class=\"ctx-label\">Stop</span><span class=\"ctx-kbd\">Tab</span></button>"
    "    <button id=\"mi-prev\" class=\"ctx-item\" onclick=\"onMenuPrev()\"><span class=\"ctx-label\">Previous track</span><span class=\"ctx-kbd\">B</span></button>"
    "    <button id=\"mi-next\" class=\"ctx-item\" onclick=\"onMenuNext()\"><span class=\"ctx-label\">Next track</span><span class=\"ctx-kbd\">N</span></button>"
    "    <div class=\"ctx-sep\"></div>"
    "    <button id=\"mi-rew\" class=\"ctx-item\" onclick=\"onMenuRew()\"><span class=\"ctx-label\">Rewind 10s</span><span class=\"ctx-kbd\">\xe2\x86\x90</span></button>"
    "    <button id=\"mi-fwd\" class=\"ctx-item\" onclick=\"onMenuFwd()\"><span class=\"ctx-label\">Forward 10s</span><span class=\"ctx-kbd\">\xe2\x86\x92</span></button>"
    "    <button id=\"mi-volup\" class=\"ctx-item\" onclick=\"onMenuVolUp()\"><span id=\"mi-volup-lbl\" class=\"ctx-label\">Volume up</span><span class=\"ctx-kbd\">\xe2\x86\x91</span></button>"
    "    <button id=\"mi-voldn\" class=\"ctx-item\" onclick=\"onMenuVolDown()\"><span id=\"mi-voldn-lbl\" class=\"ctx-label\">Volume down</span><span class=\"ctx-kbd\">\xe2\x86\x93</span></button>"
    "    <div class=\"ctx-sep\"></div>"
    "    <button id=\"mi-eq\" class=\"ctx-item\" onclick=\"onMenuToggleEq()\"><span id=\"mi-eq-lbl\" class=\"ctx-label\">Equalizer</span></button>"
    "    <button id=\"mi-pl\" class=\"ctx-item\" onclick=\"onMenuTogglePlaylist()\"><span id=\"mi-pl-lbl\" class=\"ctx-label\">Playlist editor</span></button>"
    "    <button id=\"mi-add-folder\" class=\"ctx-item\" onclick=\"onAddFolder()\"><span class=\"ctx-label\">Add folder...</span></button>"
    "    <div class=\"ctx-sep\"></div>"
    "    <button id=\"mi-xtc\" class=\"ctx-item\" onclick=\"onMenuXtc()\"><span id=\"mi-xtc-lbl\" class=\"ctx-label\">Crosstalk (XTC)</span><span class=\"ctx-kbd\">C</span></button>"
    "    <button id=\"mi-xtc-up\" class=\"ctx-item\" onclick=\"onMenuXtcUp()\"><span id=\"mi-xtc-up-lbl\" class=\"ctx-label\">XTC atten +</span><span class=\"ctx-kbd\">+</span></button>"
    "    <button id=\"mi-xtc-dn\" class=\"ctx-item\" onclick=\"onMenuXtcDown()\"><span id=\"mi-xtc-dn-lbl\" class=\"ctx-label\">XTC atten -</span><span class=\"ctx-kbd\">-</span></button>"
    "    <button id=\"mi-dsd\" class=\"ctx-item\" onclick=\"onMenuDsd()\"><span id=\"mi-dsd-lbl\" class=\"ctx-label\">DSD (DoP)</span><span class=\"ctx-kbd\">E</span></button>"
    "    <button id=\"mi-sr\" class=\"ctx-item\" onclick=\"onMenuSr()\"><span id=\"mi-sr-lbl\" class=\"ctx-label\">Super resolution</span><span class=\"ctx-kbd\">S</span></button>"
    "    <button id=\"mi-loop\" class=\"ctx-item\" onclick=\"onMenuLoop()\"><span id=\"mi-loop-lbl\" class=\"ctx-label\">Repeat playlist</span><span class=\"ctx-kbd\">L</span></button>"
    "    <button id=\"mi-fmt\" class=\"ctx-item\" onclick=\"onMenuFmt()\"><span class=\"ctx-label\">Format filter</span><span class=\"ctx-kbd\">F</span></button>"
    "    <button id=\"mi-dev\" class=\"ctx-item ctx-has-sub\" onclick=\"onMenuDevice()\"><span id=\"mi-dev-lbl\" class=\"ctx-label\">ALSA device</span><span class=\"ctx-kbd\">D</span><span class=\"ctx-sub\">\xe2\x96\xb8</span></button>"
    "    <div class=\"ctx-sep\"></div>"
    "    <button id=\"mi-skin-rand\" class=\"ctx-item\" onclick=\"onSkinRandom()\"><span id=\"mi-skin-rand-lbl\" class=\"ctx-label\">Random skin / track</span></button>"
    "    <button id=\"mi-skin-next\" class=\"ctx-item\" onclick=\"onSkinNext()\"><span class=\"ctx-label\">Next skin</span></button>"
    "    <button id=\"mi-skin-folder\" class=\"ctx-item\" onclick=\"onSkinFolder()\"><span class=\"ctx-label\">Skin folder...</span></button>"
    "    <button id=\"mi-skin-builtin\" class=\"ctx-item\" onclick=\"onSkinBuiltin()\"><span class=\"ctx-label\">Built-in Ember skin</span></button>"
    "    <div class=\"ctx-sep\"></div>"
    "    <button id=\"mi-text\" class=\"ctx-item\" onclick=\"onMenuTextSize()\"><span id=\"mi-text-lbl\" class=\"ctx-label\">Text size</span><span class=\"ctx-kbd\">T</span></button>"
    "    <button id=\"mi-about\" class=\"ctx-item\" onclick=\"onAbout()\"><span class=\"ctx-label\">About aplay+</span></button>"
    "    <button id=\"mi-exit\" class=\"ctx-item ctx-danger\" onclick=\"onMenuQuit()\"><span class=\"ctx-label\">Exit</span><span class=\"ctx-kbd\">Q</span></button>"
    "  </div>"
    "  <div id=\"surface-dev-cards\" class=\"ctx-menu ctx-submenu ctx-submenu-cards\">"
    "    <div class=\"ctx-heading\">Sound cards</div>"
    "    <button id=\"dc0\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc1\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc2\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc3\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc4\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc5\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc6\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc7\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc8\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc9\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc10\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc11\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc12\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc13\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc14\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "    <button id=\"dc15\" class=\"ctx-item ctx-has-sub ctx-slot\" onclick=\"onDevCard()\"></button>"
    "  </div>"
    "  <div id=\"surface-dev-pcms\" class=\"ctx-menu ctx-submenu ctx-submenu-pcms\">"
    "    <div id=\"pcm-heading\" class=\"ctx-heading\">Devices</div>"
    "    <button id=\"dp0\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp1\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp2\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp3\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp4\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp5\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp6\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp7\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp8\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp9\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp10\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp11\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp12\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp13\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp14\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp15\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp16\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp17\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp18\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp19\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp20\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp21\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp22\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "    <button id=\"dp23\" class=\"ctx-item ctx-slot\" onclick=\"onDevPcm()\"></button>"
    "  </div>"
    "  <div id=\"surface-about\" class=\"about-dialog\">"
    "    <button class=\"about-close\" aria-label=\"Close\" onclick=\"onAboutClose()\">\xc3\x97</button>"
    "    <div class=\"about-hero\">"
    "      <div class=\"about-mark\">&#9835;</div>"
    "      <div class=\"about-brand\">aplay+</div>"
    "      <div class=\"about-tag\"><span class=\"about-dot\"></span>Ember Edition</div>"
    "    </div>"
    "    <div class=\"about-sep\"></div>"
    "    <div class=\"about-body\">Lightweight audiophile player with a Winamp-classic soul. FLAC, MP3, AAC, OGG, WAV, WMA and DSD — with optional realtime PCM&#8594;DSD64.</div>"
    "    <div class=\"about-specs\">"
    "      <div class=\"about-spec\"><span class=\"about-spec-k\">License</span><span class=\"about-spec-v\">MIT</span></div>"
    "      <div class=\"about-spec\"><span class=\"about-spec-k\">Author</span><span class=\"about-spec-v\">Yuichiro Nakada</span></div>"
    "      <div class=\"about-spec\"><span class=\"about-spec-k\">UI</span><span class=\"about-spec-v\">luna-ui + Winamp skins</span></div>"
    "    </div>"
    "    <button class=\"about-ok\" onclick=\"onAboutClose()\">Close</button>"
    "  </div>"
    "</div>";

static const char *WINAMP_CSS =
    "/* aplay+ Ember — warm charcoal chassis, copper accents, multi-surface shell */"
    ":root{--skin-window:#000;--skin-main-ink:#f3e6d4;--skin-main-muted:#b89a7a;--skin-main-data:#e8a868;--skin-main-shadow:rgba(0,0,0,.55);--skin-meter-bg:rgba(0,0,0,.35);--skin-focus:#ffd39a;--skin-font:'DejaVu Sans Mono',monospace;"
    "--playlist-normal:#E4D6C5;--playlist-current:#FFE6B8;--playlist-bg:#14110F;--playlist-selected-bg:#5C3A22;}"
    "html,body{width:875px;height:800px;margin:0;padding:0;overflow:hidden;}body{background:transparent;font-family:var(--skin-font);}"
    ".winamp-app{position:relative;width:875px;height:800px;margin:0;padding:0;}"
    ".skin-surface{position:absolute;width:275px;background-position:0 0;background-repeat:no-repeat;}"
    ".skin-main{left:0;top:0;height:116px;background-size:275px 116px;}"
    ".skin-state,.skin-track-count,.skin-time,.skin-time-total,.skin-title,.skin-subtitle,.skin-codec,.skin-rate,.skin-volume-value{position:absolute;color:var(--skin-main-ink);font-family:var(--skin-font);font-weight:700;text-shadow:0 1px 0 var(--skin-main-shadow);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}"
    ".skin-state{left:24px;top:15px;font-size:6px;} .skin-track-count{left:24px;top:25px;width:22px;font-size:7px;}"
    ".skin-time{left:48px;top:23px;width:44px;font-size:14px;} .skin-time-total{left:48px;top:40px;width:44px;font-size:6px;color:var(--skin-main-muted);}"
    ".skin-title{left:111px;top:24px;width:148px;height:9px;font-size:7px;line-height:9px;} .skin-subtitle{left:111px;top:35px;width:148px;font-size:6px;color:var(--skin-main-muted);}"
    ".skin-codec{left:111px;top:44px;width:40px;font-size:6px;} .skin-rate{left:152px;top:44px;width:107px;font-size:6px;color:var(--skin-main-muted);}"
    ".skin-progress-track{position:absolute;left:16px;top:72px;width:249px;height:5px;background:var(--skin-meter-bg);overflow:hidden;} .skin-progress-fill{width:0%;height:5px;background:var(--skin-main-data);opacity:.88;}"
    ".skin-volume-track{position:absolute;left:107px;top:58px;width:68px;height:4px;background:var(--skin-meter-bg);overflow:hidden;} .skin-volume-fill{width:100%;height:4px;background:var(--skin-main-data);opacity:.88;}"
    ".skin-volume-value{left:180px;top:55px;width:28px;font-size:6px;}"
    ".skin-btn{position:absolute;top:88px;height:18px;padding:0;border:0;border-radius:0;background-color:transparent;background-size:100% 100%;cursor:pointer;}"
    ".skin-prev{left:16px;width:23px}.skin-play{left:39px;width:23px}.skin-pause{left:62px;width:23px}.skin-stop{left:85px;width:23px}.skin-next{left:108px;width:22px}.skin-eject{left:136px;width:22px}"
    ".skin-panel-toggle{position:absolute;top:58px;width:22px;height:12px;padding:0;border:0;background:transparent;color:transparent;font-size:1px;cursor:pointer}.skin-toggle-eq{left:219px}.skin-toggle-pl{left:242px}"
    ".skin-window-close{position:absolute;z-index:5;left:264px;top:3px;width:9px;height:9px;padding:0;border:0;background:transparent;cursor:pointer;}"
    ".skin-btn:focus-visible,.skin-panel-toggle:focus-visible,.skin-window-close:focus-visible,.ctx-item:focus-visible,.about-ok:focus-visible,.about-close:focus-visible{outline:1px solid var(--skin-focus);outline-offset:0;}"
    ".skin-equalizer{left:300px;top:0;height:116px;background-size:275px 116px;}"
    ".eq-status{position:absolute;left:14px;top:20px;color:var(--skin-main-ink);font:700 7px var(--skin-font);text-shadow:0 1px var(--skin-main-shadow)}"
    ".eq-analyzer{position:absolute;left:15px;top:34px;width:52px;height:58px;display:flex;align-items:flex-end;gap:2px;overflow:hidden}.eq-analyzer i{display:block;width:2px;background:var(--skin-main-data);opacity:.75}.eq-analyzer i:nth-child(3n+1){height:72%}.eq-analyzer i:nth-child(3n+2){height:45%}.eq-analyzer i:nth-child(3n){height:86%}"
    ".eq-sliders{position:absolute;left:78px;top:38px;width:178px;height:60px;display:flex;justify-content:space-between}.eq-sliders i{position:relative;display:block;width:7px;height:60px}.eq-sliders i:before{content:'';position:absolute;left:3px;top:0;width:1px;height:60px;background:var(--skin-meter-bg)}.eq-sliders i:after{content:'';position:absolute;left:0;top:27px;width:7px;height:5px;background:var(--skin-main-data);box-shadow:0 1px var(--skin-main-shadow)}"
    ".skin-playlist{left:600px;top:0;height:232px;background-size:275px 232px;}"
    ".skin-list{position:absolute;left:12px;top:23px;width:243px;height:168px;overflow:hidden;}"
    ".skin-list-row{width:239px;height:12px;padding:0 2px;font-family:var(--skin-font);font-size:10px;font-weight:700;line-height:12px;color:var(--playlist-normal);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;cursor:pointer;}"
    ".skin-list-row.selected{background-color:var(--playlist-selected-bg);color:var(--playlist-current);}"
    ".skin-list-row.current{color:var(--playlist-current);box-shadow:inset 2px 0 0 var(--skin-main-data);}"
    ".skin-list-row.current.selected{background-color:var(--playlist-selected-bg);}"
    ".skin-scroll-track{position:absolute;left:257px;top:23px;width:8px;height:168px;cursor:ns-resize}.skin-scroll-thumb{position:absolute;left:0;top:0;width:8px;min-height:12px;background:var(--skin-main-data);opacity:.72;cursor:ns-resize}"
    ".skin-device,.skin-note{position:absolute;top:217px;height:10px;font-family:var(--skin-font);font-size:6px;font-weight:700;line-height:9px;color:var(--playlist-normal);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}"
    ".skin-device{left:14px;width:100px;cursor:pointer}.skin-note{left:120px;width:141px;text-align:right;}"
    ".ctx-menu{position:absolute;left:0;top:250px;width:292px;height:440px;padding:4px 0;background:#1a1612;border:1px solid #5c3a22;box-shadow:2px 2px 0 rgba(0,0,0,.45);z-index:50;}"
    /* Compact native popup: flex rows with kbd chips on the trailing edge. */
    ".ctx-item{display:flex;flex-direction:row;align-items:center;justify-content:flex-start;gap:8px;width:100%;height:15px;padding:0 10px;border:0;background:transparent;color:#E4D6C5;font:11px/15px 'DejaVu Sans',sans-serif;text-align:left;cursor:pointer;white-space:nowrap;overflow:hidden;}"
    ".ctx-item:hover{background:#5C3A22;color:#FFE6B8;}"
    ".ctx-item:hover .ctx-kbd{border-color:#e8a868;color:#FFE6B8;background:rgba(232,168,104,.14);}"
    ".ctx-item.checked{color:#FFE6B8;}"
    ".ctx-item.ctx-slot{display:none;}"
    ".ctx-item.ctx-slot.ctx-show{display:flex;}"
    ".ctx-label{flex:1 1 auto;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;pointer-events:none;}"
    ".ctx-kbd{flex:0 0 auto;min-width:18px;height:12px;padding:0 5px;border:1px solid #4a3426;border-radius:3px;background:rgba(0,0,0,.38);color:#b8956e;font:600 9px/12px 'DejaVu Sans Mono',monospace;letter-spacing:0.04em;text-align:center;pointer-events:none;}"
    ".ctx-sub{flex:0 0 auto;color:#9a7f66;font:10px/15px 'DejaVu Sans',sans-serif;pointer-events:none;}"
    ".ctx-danger{color:#e8a0a0;}"
    ".ctx-danger .ctx-kbd{color:#e8a0a0;border-color:#6a3a3a;}"
    ".ctx-sep{height:1px;margin:2px 10px;background:#5c3a22;opacity:.85;}"
    ".ctx-submenu{z-index:55;}"
    ".ctx-submenu-cards{left:302px;top:250px;width:240px;height:280px;}"
    ".ctx-submenu-pcms{left:552px;top:250px;width:240px;height:280px;}"
    ".ctx-heading{height:18px;padding:0 10px;color:#9a7f66;font:700 9px/18px 'DejaVu Sans',sans-serif;letter-spacing:0.06em;text-transform:uppercase;}"
    /* Overlay text scales (Classic = skin-native 1x sizes). */
    "#app.ts-1 .skin-state{font-size:8px;}#app.ts-1 .skin-track-count{font-size:9px;}"
    "#app.ts-1 .skin-time{font-size:16px;}#app.ts-1 .skin-time-total{font-size:8px;}"
    "#app.ts-1 .skin-title{font-size:9px;height:11px;line-height:11px;}#app.ts-1 .skin-subtitle{font-size:8px;}"
    "#app.ts-1 .skin-codec,#app.ts-1 .skin-rate,#app.ts-1 .skin-volume-value{font-size:8px;}"
    "#app.ts-1 .skin-list-row{font-size:11px;height:13px;line-height:13px;}#app.ts-1 .skin-device,#app.ts-1 .skin-note{font-size:8px;}"
    "#app.ts-1 .eq-status{font:700 9px var(--skin-font);}"
    "#app.ts-2 .skin-state{font-size:9px;}#app.ts-2 .skin-track-count{font-size:10px;}"
    "#app.ts-2 .skin-time{font-size:17px;}#app.ts-2 .skin-time-total{font-size:9px;}"
    "#app.ts-2 .skin-title{font-size:10px;height:12px;line-height:12px;}#app.ts-2 .skin-subtitle{font-size:9px;}"
    "#app.ts-2 .skin-codec,#app.ts-2 .skin-rate,#app.ts-2 .skin-volume-value{font-size:9px;}"
    "#app.ts-2 .skin-list-row{font-size:12px;height:14px;line-height:14px;}#app.ts-2 .skin-device,#app.ts-2 .skin-note{font-size:9px;}"
    "#app.ts-2 .eq-status{font:700 10px var(--skin-font);}"
    "#app.ts-3 .skin-state{font-size:10px;}#app.ts-3 .skin-track-count{font-size:11px;}"
    "#app.ts-3 .skin-time{font-size:18px;}#app.ts-3 .skin-time-total{font-size:10px;}"
    "#app.ts-3 .skin-title{font-size:11px;height:13px;line-height:13px;}#app.ts-3 .skin-subtitle{font-size:10px;}"
    "#app.ts-3 .skin-codec,#app.ts-3 .skin-rate,#app.ts-3 .skin-volume-value{font-size:10px;}"
    "#app.ts-3 .skin-list-row{font-size:13px;height:15px;line-height:15px;}#app.ts-3 .skin-device,#app.ts-3 .skin-note{font-size:10px;}"
    "#app.ts-3 .eq-status{font:700 11px var(--skin-font);}"
    ".about-dialog{position:absolute;left:520px;top:680px;width:340px;height:380px;padding:32px 24px 18px;display:flex;flex-direction:column;align-items:stretch;box-sizing:border-box;background:linear-gradient(165deg,#241e19 0%,#1a1612 48%,#12100e 100%);border:1px solid #7a4a28;box-shadow:0 18px 40px rgba(0,0,0,.55),inset 0 1px 0 rgba(255,214,170,.12);z-index:60;}"
    ".about-close{position:absolute;right:8px;top:8px;width:18px;height:18px;padding:0;border:1px solid #5c3a22;border-radius:4px;background:#2a221c;color:#E4D6C5;font:700 14px/18px 'DejaVu Sans',sans-serif;text-align:center;cursor:pointer;z-index:10;}"
    ".about-close:hover{background:#3a2a20;color:#FFE6B8;}"
    ".about-hero{display:flex;flex-direction:column;align-items:center;flex:0 0 auto;width:100%;margin:0 0 10px;}"
    ".about-mark{width:52px;height:52px;margin:0 0 8px;border-radius:14px;background:linear-gradient(145deg,#e8a868,#a85a28);box-shadow:0 10px 24px rgba(168,90,40,.35),inset 0 1px 0 rgba(255,255,255,.25);color:#1a120c;font:700 28px/52px 'DejaVu Serif',serif;text-align:center;}"
    ".about-brand{margin:0 0 6px;color:#FFE6B8;font:700 26px/1 'DejaVu Serif',serif;letter-spacing:-0.02em;text-align:center;}"
    ".about-tag{display:flex;flex-direction:row;align-items:center;justify-content:center;gap:6px;margin:0;padding:3px 10px;border:1px solid rgba(232,168,104,.35);border-radius:999px;background:rgba(232,168,104,.10);color:#E4D6C5;font:600 10px/1.2 'DejaVu Sans',sans-serif;letter-spacing:0.04em;}"
    ".about-dot{width:5px;height:5px;border-radius:50%;background:#e8a868;flex:0 0 5px;}"
    ".about-sep{width:100%;height:1px;margin:0 0 10px;flex:0 0 auto;background:linear-gradient(90deg,transparent,#5c3a22,transparent);}"
    ".about-body{width:100%;margin:0 0 12px;flex:0 0 auto;color:#cbb7a2;font:13px/1.45 'DejaVu Sans',sans-serif;}"
    ".about-specs{width:100%;margin:0;flex:1 1 auto;display:flex;flex-direction:column;gap:6px;}"
    ".about-spec{display:flex;flex-direction:row;justify-content:space-between;align-items:center;height:28px;margin:0;padding:0 10px;border:1px solid #3a2a20;border-radius:8px;background:rgba(255,255,255,.03);flex:0 0 28px;}"
    ".about-spec-k{color:#9a7f66;font:700 9px/1 'DejaVu Sans',sans-serif;letter-spacing:0.06em;text-transform:uppercase;}"
    ".about-spec-v{color:#FFE6B8;font:600 11px/1 'DejaVu Sans',sans-serif;}"
    ".about-ok{width:100%;height:34px;margin:12px 0 0;flex:0 0 34px;border:1px solid #7a4a28;border-radius:8px;background:linear-gradient(180deg,#5c3a22,#3a2416);color:#FFE6B8;font:700 12px/34px 'DejaVu Sans',sans-serif;cursor:pointer;}"
    ".about-ok:hover{background:linear-gradient(180deg,#6d4528,#4a2e1a);}"
    "@media(prefers-reduced-motion:reduce){.skin-btn,.ctx-item,.about-ok{transition:none;}}";

static void build_winamp_image_css(char *out, size_t out_n)
{
	snprintf(out, out_n,
		":root{--playlist-normal:%s;--playlist-current:%s;--playlist-bg:%s;--playlist-selected-bg:%s;}"
		".skin-main{background-image:url(%s);}.skin-equalizer{background-image:url(%s);}.skin-playlist{background-color:var(--playlist-bg);background-image:url(%s);}"
		".skin-prev{background-image:url(%s);}.skin-play{background-image:url(%s);}.skin-pause{background-image:url(%s);}.skin-stop{background-image:url(%s);}.skin-next{background-image:url(%s);}.skin-eject{background-image:url(%s);}"
		".skin-prev:active{background-image:url(%s);}.skin-play:active{background-image:url(%s);}.skin-pause:active{background-image:url(%s);}.skin-stop:active{background-image:url(%s);}.skin-next:active{background-image:url(%s);}.skin-eject:active{background-image:url(%s);}",
		g_skin.playlist_normal, g_skin.playlist_current, g_skin.playlist_bg, g_skin.playlist_selected_bg,
		g_skin.main_png, g_skin.equalizer_png, g_skin.playlist_png,
		g_skin.prev_png, g_skin.play_png, g_skin.pause_png, g_skin.stop_png, g_skin.next_png, g_skin.eject_png,
		g_skin.prev_down_png, g_skin.play_down_png, g_skin.pause_down_png, g_skin.stop_down_png, g_skin.next_down_png, g_skin.eject_down_png);
}

static void onPrev(LunaElement *e)       { (void)e; gui_inject_key('b'); }
static void onNext(LunaElement *e)       { (void)e; gui_inject_key('n'); }
static void onPlayPause(LunaElement *e)  { (void)e; gui_inject_key(0x20); }
static void onStop(LunaElement *e)       { (void)e; gui_inject_key(0x09); } /* Tab: hard stop */
static void onRew(LunaElement *e)        { (void)e; gui_inject_key(KEY_LEFT); }
static void onFwd(LunaElement *e)        { (void)e; gui_inject_key(KEY_RIGHT); }
static void onVolUp(LunaElement *e)      { (void)e; gui_inject_key(KEY_UP); }
static void onVolDown(LunaElement *e)    { (void)e; gui_inject_key(KEY_DOWN); }
static void onXtc(LunaElement *e)        { (void)e; gui_inject_key('c'); }
static void onXtcUp(LunaElement *e)      { (void)e; gui_inject_key('+'); }
static void onXtcDown(LunaElement *e)    { (void)e; gui_inject_key('-'); }
static void onDsd(LunaElement *e)        { (void)e; gui_inject_key('e'); }
static void onSr(LunaElement *e)         { (void)e; gui_inject_key('s'); }
static void onFmt(LunaElement *e)        { (void)e; gui_inject_key('f'); }
static void onLoop(LunaElement *e)       { (void)e; gui_inject_key('l'); }
static void onQuit(LunaElement *e)       { (void)e; gui_inject_key('q'); gui_request_close(); }
static void onDevicePicker(LunaElement *e)
{
	(void)e;
	int on = __atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST);
	gui_set_device_picker(!on);
}
/* Playlist rows are thin (12px). Luna's press-scale hit test often misses the
 * matching release, so the GLFW surface maps Y → row directly. */
static int gui_playlist_row_at(double lx, double ly)
{
	if (lx < 12.0 || lx >= 255.0) return -1;
	if (ly < 23.0 || ly >= 23.0 + 13.0 * 12.0) return -1;
	int row = (int)((ly - 23.0) / 12.0);
	return (row >= 0 && row < 13) ? row : -1;
}

static void gui_handle_playlist_row(int row)
{
	if (row < 0 || row >= 13) return;
	pthread_mutex_lock(&g_gui.lock);
	int item = g_gui.playlist_first + row;
	char label[256];
	snprintf(label, sizeof(label), "%s",
		(row >= 0 && row < 13) ? g_gui.playlist_lines[row] : "");
	pthread_mutex_unlock(&g_gui.lock);
	if (!label[0]) return;

	/* Highlight immediately; activate (play / apply device) on the same click
	 * so the list feels like a real file picker rather than requiring a
	 * double-click that luna-ui often drops. */
	g_playlist_last_click_item = item;
	g_playlist_last_click_time = glfwGetTime();
	if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
		if (item < aplay_device_count()) gui_select_device_item(item, 1);
		return;
	}
	if (item < g_playlist_name_count) gui_select_playlist_item(item, 1);
}

static void onPlaylistItem(LunaElement *e)
{
	if (!e || strncmp(e->id, "pl", 2) != 0) return;
	char *end = NULL;
	long row = strtol(e->id + 2, &end, 10);
	if (!end || *end || row < 0 || row >= 13) return;
	gui_handle_playlist_row((int)row);
}

enum {
	AUI_MAIN, AUI_EQUALIZER, AUI_PLAYLIST, AUI_MENU,
	AUI_DEV_CARDS, AUI_DEV_PCMS, AUI_ABOUT, AUI_SURFACE_COUNT
};
#define AUI_MAX_DEV_CARDS 16
#define AUI_MAX_DEV_PCMS  24
/* Approximate Y of the "ALSA device" row inside the context menu window. */
#define AUI_DEV_ITEM_Y 289
#define AUI_MENU_W 292
#define AUI_MENU_H 432
#define AUI_TEXT_SCALE_COUNT 4
typedef struct {
	GLFWwindow *window;
	LunaContext *context;
	int kind, width, height;
	float origin_x, origin_y;
	int visible, dragging, resizing, playlist_thumb_dragging;
	double drag_x, drag_y;
	double resize_screen_x, resize_screen_y;
	int resize_width, resize_height;
} AuiSurface;
static AuiSurface g_surfaces[AUI_SURFACE_COUNT];
static GLFWcursor *g_resize_cursor = NULL;
static int g_dev_card_ids[AUI_MAX_DEV_CARDS];
static int g_dev_card_count = 0;
static int g_dev_pcm_idxs[AUI_MAX_DEV_PCMS];
static int g_dev_pcm_count = 0;
static int g_dev_menu_card = -1;
static int g_el_dc[AUI_MAX_DEV_CARDS];
static int g_el_dp[AUI_MAX_DEV_PCMS];
static int g_el_pcm_heading = -1;
static int g_el_mi_dev = -1;
static int g_el_app = -1;
static int g_el_mi_text = -1;
static int g_text_scale = 1; /* 0 Classic .. 3 Extra; default Comfortable */
static const char *const g_text_scale_names[AUI_TEXT_SCALE_COUNT] = {
	"Classic", "Comfortable", "Large", "Extra"
};
static const char *const g_text_scale_classes[AUI_TEXT_SCALE_COUNT] = {
	"ts-0", "ts-1", "ts-2", "ts-3"
};

static void aui_hide_context_menu(void);
static void aui_hide_device_menus(void);
static void aui_refresh_context_menu_labels(void);
static void aui_show_device_card_menu(void);
static void aui_show_device_pcm_menu(int card);
static void aui_apply_prepared_skin_to_ui(void);
static void aui_apply_text_scale(void);
static int aui_load_skin_path(const char *path, int is_builtin);
static int aui_is_popup_surface(int kind);

static void aui_set_surface_visible(int kind, int visible)
{
	if (kind < 0 || kind >= AUI_SURFACE_COUNT || !g_surfaces[kind].window) return;
	g_surfaces[kind].visible = visible;
	if (visible) {
		glfwSetWindowShouldClose(g_surfaces[kind].window, GLFW_FALSE);
		glfwShowWindow(g_surfaces[kind].window);
	} else {
		glfwHideWindow(g_surfaces[kind].window);
	}
}
static void onToggleEq(LunaElement *e)       { (void)e; aui_set_surface_visible(AUI_EQUALIZER, !g_surfaces[AUI_EQUALIZER].visible); }
static void onTogglePlaylist(LunaElement *e) { (void)e; aui_set_surface_visible(AUI_PLAYLIST, !g_surfaces[AUI_PLAYLIST].visible); }
static void onHideEq(LunaElement *e)         { (void)e; aui_set_surface_visible(AUI_EQUALIZER, 0); }
static void onHidePlaylist(LunaElement *e)   { (void)e; aui_set_surface_visible(AUI_PLAYLIST, 0); }

static void aui_menu_run(void (*fn)(LunaElement *), LunaElement *e)
{
	aui_hide_context_menu();
	if (fn) fn(e);
}
static void onMenuPlayPause(LunaElement *e) { aui_menu_run(onPlayPause, e); }
static void onMenuStop(LunaElement *e)      { aui_menu_run(onStop, e); }
static void onMenuPrev(LunaElement *e)      { aui_menu_run(onPrev, e); }
static void onMenuNext(LunaElement *e)      { aui_menu_run(onNext, e); }
static void onMenuRew(LunaElement *e)       { aui_menu_run(onRew, e); }
static void onMenuFwd(LunaElement *e)       { aui_menu_run(onFwd, e); }
static void onMenuVolUp(LunaElement *e)
{
	onVolUp(e);
	aui_refresh_context_menu_labels();
}
static void onMenuVolDown(LunaElement *e)
{
	onVolDown(e);
	aui_refresh_context_menu_labels();
}
static void onMenuToggleEq(LunaElement *e)  { aui_menu_run(onToggleEq, e); }
static void onMenuTogglePlaylist(LunaElement *e) { aui_menu_run(onTogglePlaylist, e); }
static void onMenuXtc(LunaElement *e)       { aui_menu_run(onXtc, e); }
static void onMenuXtcUp(LunaElement *e)
{
	onXtcUp(e);
	aui_refresh_context_menu_labels();
}
static void onMenuXtcDown(LunaElement *e)
{
	onXtcDown(e);
	aui_refresh_context_menu_labels();
}
static void onMenuDsd(LunaElement *e)       { aui_menu_run(onDsd, e); }
static void onMenuSr(LunaElement *e)        { aui_menu_run(onSr, e); }
static void onMenuLoop(LunaElement *e)      { aui_menu_run(onLoop, e); }
static void onMenuFmt(LunaElement *e)       { aui_menu_run(onFmt, e); }
static void onMenuDevice(LunaElement *e)
{
	(void)e;
	/* Keep the main menu open; cascade card → PCM device flyouts. */
	aui_show_device_card_menu();
}
static void onMenuQuit(LunaElement *e)      { aui_menu_run(onQuit, e); }

static void aui_apply_text_scale(void)
{
	if (g_el_app < 0) g_el_app = luna_get_element_by_id("app");
	if (g_el_app < 0) return;
	if (g_text_scale < 0) g_text_scale = 0;
	if (g_text_scale >= AUI_TEXT_SCALE_COUNT) g_text_scale = AUI_TEXT_SCALE_COUNT - 1;
	for (int i = 0; i < AUI_TEXT_SCALE_COUNT; i++)
		luna_remove_class(g_el_app, g_text_scale_classes[i]);
	luna_add_class(g_el_app, g_text_scale_classes[g_text_scale]);
	pthread_mutex_lock(&g_gui.lock);
	snprintf(g_gui.note, sizeof(g_gui.note), "Text size · %s",
		g_text_scale_names[g_text_scale]);
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void onMenuTextSize(LunaElement *e)
{
	(void)e;
	g_text_scale = (g_text_scale + 1) % AUI_TEXT_SCALE_COUNT;
	aui_apply_text_scale();
	aui_refresh_context_menu_labels();
}

static int aui_is_popup_surface(int kind)
{
	return kind == AUI_MENU || kind == AUI_DEV_CARDS || kind == AUI_DEV_PCMS || kind == AUI_ABOUT;
}

static void aui_hide_device_menus(void)
{
	if (g_surfaces[AUI_DEV_PCMS].window && g_surfaces[AUI_DEV_PCMS].visible)
		aui_set_surface_visible(AUI_DEV_PCMS, 0);
	if (g_surfaces[AUI_DEV_CARDS].window && g_surfaces[AUI_DEV_CARDS].visible)
		aui_set_surface_visible(AUI_DEV_CARDS, 0);
	g_dev_menu_card = -1;
}

static void aui_set_slot_visible(int idx, int on)
{
	if (idx < 0) return;
	if (on) luna_add_class(idx, "ctx-show");
	else luna_remove_class(idx, "ctx-show");
}

static void aui_position_flyout(int kind, int anchor_kind, int y_offset)
{
	if (!g_surfaces[kind].window || !g_surfaces[anchor_kind].window) return;
	int ax = 0, ay = 0, aw = 0, ah = 0;
	glfwGetWindowPos(g_surfaces[anchor_kind].window, &ax, &ay);
	glfwGetWindowSize(g_surfaces[anchor_kind].window, &aw, &ah);
	(void)ah;
	int x = ax + aw - 4;
	int y = ay + y_offset;
	if (y < 0) y = 0;
	glfwSetWindowPos(g_surfaces[kind].window, x, y);
}

static void aui_show_device_pcm_menu(int card)
{
	aplay_refresh_devices();
	g_dev_menu_card = card;
	g_dev_pcm_count = 0;

	const char *card_label = NULL;
	char heading[96];
	int n = aplay_device_count();
	for (int i = 0; i < n && g_dev_pcm_count < AUI_MAX_DEV_PCMS; i++) {
		if (aplay_device_card(i) != card) continue;
		g_dev_pcm_idxs[g_dev_pcm_count] = i;
		if (!card_label) card_label = aplay_device_card_name(i);
		g_dev_pcm_count++;
	}
	if (card_label && card_label[0])
		snprintf(heading, sizeof(heading), "%s", card_label);
	else if (card >= 0)
		snprintf(heading, sizeof(heading), "card %d", card);
	else
		snprintf(heading, sizeof(heading), "Devices");
	if (g_el_pcm_heading >= 0) luna_set_text(g_el_pcm_heading, heading);

	pthread_mutex_lock(&g_gui.lock);
	char cur_dev[64];
	snprintf(cur_dev, sizeof(cur_dev), "%s", g_gui.device[0] ? g_gui.device : g_dev_storage);
	pthread_mutex_unlock(&g_gui.lock);

	for (int i = 0; i < AUI_MAX_DEV_PCMS; i++) {
		if (i < g_dev_pcm_count) {
			int di = g_dev_pcm_idxs[i];
			const char *name = aplay_device_name(di);
			int pcm = aplay_device_pcm(di);
			char label[96];
			if (pcm >= 0)
				snprintf(label, sizeof(label), "hw:%d,%d", card, pcm);
			else if (name)
				snprintf(label, sizeof(label), "%s", name);
			else
				snprintf(label, sizeof(label), "device %d", i);
			int is_cur = name && cur_dev[0] && !strcmp(name, cur_dev);
			char shown[112];
			snprintf(shown, sizeof(shown), "%s%s", is_cur ? "\xE2\x9C\x93 " : "  ", label);
			luna_set_text(g_el_dp[i], shown);
			if (is_cur) luna_add_class(g_el_dp[i], "checked");
			else luna_remove_class(g_el_dp[i], "checked");
			aui_set_slot_visible(g_el_dp[i], 1);
		} else {
			luna_set_text(g_el_dp[i], "");
			luna_remove_class(g_el_dp[i], "checked");
			aui_set_slot_visible(g_el_dp[i], 0);
		}
	}

	int anchor = g_surfaces[AUI_DEV_CARDS].visible ? AUI_DEV_CARDS : AUI_MENU;
	int y_off = (anchor == AUI_MENU) ? AUI_DEV_ITEM_Y : 0;
	aui_position_flyout(AUI_DEV_PCMS, anchor, y_off);
	aui_set_surface_visible(AUI_DEV_PCMS, 1);
	glfwFocusWindow(g_surfaces[AUI_DEV_PCMS].window);
}

static void aui_show_device_card_menu(void)
{
	aplay_refresh_devices();
	g_dev_card_count = aplay_device_unique_cards(g_dev_card_ids, AUI_MAX_DEV_CARDS);
	if (g_dev_card_count <= 0) {
		aui_hide_device_menus();
		return;
	}

	pthread_mutex_lock(&g_gui.lock);
	char cur_dev[64];
	snprintf(cur_dev, sizeof(cur_dev), "%s", g_gui.device[0] ? g_gui.device : g_dev_storage);
	pthread_mutex_unlock(&g_gui.lock);
	int cur_card = -2;
	{
		int n = aplay_device_count();
		for (int i = 0; i < n; i++) {
			const char *name = aplay_device_name(i);
			if (name && cur_dev[0] && !strcmp(name, cur_dev)) {
				cur_card = aplay_device_card(i);
				break;
			}
		}
	}

	for (int i = 0; i < AUI_MAX_DEV_CARDS; i++) {
		if (i < g_dev_card_count) {
			int card = g_dev_card_ids[i];
			const char *cname = NULL;
			int n = aplay_device_count();
			for (int d = 0; d < n; d++) {
				if (aplay_device_card(d) == card) {
					cname = aplay_device_card_name(d);
					break;
				}
			}
			char label[96];
			if (cname && cname[0])
				snprintf(label, sizeof(label), "hw:%d · %s", card, cname);
			else
				snprintf(label, sizeof(label), "hw:%d", card);
			int is_cur = (card == cur_card);
			char shown[112];
			snprintf(shown, sizeof(shown), "%s%s", is_cur ? "\xE2\x9C\x93 " : "  ", label);
			luna_set_text(g_el_dc[i], shown);
			if (is_cur) luna_add_class(g_el_dc[i], "checked");
			else luna_remove_class(g_el_dc[i], "checked");
			aui_set_slot_visible(g_el_dc[i], 1);
		} else {
			luna_set_text(g_el_dc[i], "");
			luna_remove_class(g_el_dc[i], "checked");
			aui_set_slot_visible(g_el_dc[i], 0);
		}
	}

	aui_set_surface_visible(AUI_DEV_PCMS, 0);
	aui_position_flyout(AUI_DEV_CARDS, AUI_MENU, AUI_DEV_ITEM_Y);
	aui_set_surface_visible(AUI_DEV_CARDS, 1);
	/* Also open the PCM flyout for the active (or first) card so the
	 * hierarchy is visible without an extra click. */
	{
		int open_card = g_dev_card_ids[0];
		if (cur_card != -2) {
			for (int i = 0; i < g_dev_card_count; i++) {
				if (g_dev_card_ids[i] == cur_card) {
					open_card = cur_card;
					break;
				}
			}
		}
		aui_show_device_pcm_menu(open_card);
	}
	glfwFocusWindow(g_surfaces[AUI_DEV_CARDS].window);
}

static void onDevCard(LunaElement *e)
{
	if (!e || strncmp(e->id, "dc", 2) != 0) return;
	char *end = NULL;
	long row = strtol(e->id + 2, &end, 10);
	if (!end || *end || row < 0 || row >= g_dev_card_count) return;
	aui_show_device_pcm_menu(g_dev_card_ids[(int)row]);
}

static void onDevPcm(LunaElement *e)
{
	if (!e || strncmp(e->id, "dp", 2) != 0) return;
	char *end = NULL;
	long row = strtol(e->id + 2, &end, 10);
	if (!end || *end || row < 0 || row >= g_dev_pcm_count) return;
	int di = g_dev_pcm_idxs[(int)row];
	__atomic_store_n(&g_device_select_idx, di, __ATOMIC_SEQ_CST);
	gui_inject_key('D');
	aui_hide_context_menu();
}

static void aui_request_skin_path(const char *path)
{
	if (!path || !path[0]) return;
	snprintf(g_pending_skin_path, sizeof(g_pending_skin_path), "%s", path);
	g_pending_skin_reload = 1;
}

static int aui_pick_directory(const char *title, char *out, size_t out_n)
{
	if (!out || out_n == 0) return 0;
	out[0] = '\0';
	char cmd[512];
	snprintf(cmd, sizeof(cmd),
		"zenity --file-selection --directory --title='%s' 2>/dev/null",
		title ? title : "aplay+");
	FILE *fp = popen(cmd, "r");
	if (fp) {
		if (fgets(out, (int)out_n, fp))
			out[strcspn(out, "\r\n")] = '\0';
		pclose(fp);
	}
	if (out[0]) return 1;
	snprintf(cmd, sizeof(cmd),
		"kdialog --getexistingdirectory . --title '%s' 2>/dev/null",
		title ? title : "aplay+");
	fp = popen(cmd, "r");
	if (fp) {
		if (fgets(out, (int)out_n, fp))
			out[strcspn(out, "\r\n")] = '\0';
		pclose(fp);
	}
	return out[0] != '\0';
}

static void onSkinRandom(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	if (g_skin_pack_count <= 0) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "Set a skin folder first");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	g_skin_random = !g_skin_random;
	if (g_skin_random) {
		xoroshiro128plus_init((uint64_t)time(NULL) ^ ((uint64_t)getpid() << 16));
		const char *pick = skin_pack_pick(1);
		if (pick) aui_request_skin_path(pick);
	}
	pthread_mutex_lock(&g_gui.lock);
	snprintf(g_gui.note, sizeof(g_gui.note), "%s",
		g_skin_random ? "Random skin per track on" : "Random skin per track off");
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void onSkinNext(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	const char *pick = skin_pack_pick(1);
	if (!pick) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "No skins in pack");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	aui_request_skin_path(pick);
}

static void onSkinBuiltin(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	g_pending_skin_path[0] = '\0';
	g_pending_skin_reload = 2; /* 2 = builtin */
}

static void onSkinFolder(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	char chosen[PATH_MAX] = "";
	if (!aui_pick_directory("aplay+ skin folder", chosen, sizeof(chosen))) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s",
			"Skin folder: use -R <dir> (no dialog)");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	if (scan_skins_folder(chosen) <= 0) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "No skins in that folder");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	snprintf(g_skins_folder, sizeof(g_skins_folder), "%s", chosen);
	g_skin_random = 1;
	xoroshiro128plus_init((uint64_t)time(NULL) ^ ((uint64_t)getpid() << 16));
	const char *pick = skin_pack_pick(0);
	if (pick) aui_request_skin_path(pick);
	pthread_mutex_lock(&g_gui.lock);
	snprintf(g_gui.note, sizeof(g_gui.note), "Skins: %d in pack", g_skin_pack_count);
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static void onAddFolder(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	char chosen[PATH_MAX] = "";
	if (!aui_pick_directory("aplay+ add folder", chosen, sizeof(chosen))) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "Add folder cancelled");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	int before = g_playlist_root_count;
	playlist_root_add(chosen);
	if (g_playlist_root_count == before) {
		pthread_mutex_lock(&g_gui.lock);
		snprintf(g_gui.note, sizeof(g_gui.note), "%s", "Folder already in playlist");
		g_gui.version++;
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}

	/* Optimistic UI update; the engine merges on the next track boundary. */
	int part_n = 0;
	int scan_flag = g_start_flag & LS_RECURSIVE;
	LS_LIST *part = ls_dir(chosen, scan_flag, &part_n);
	int added = 0;
	pthread_mutex_lock(&g_gui.lock);
	if (part) {
		for (int i = 0; i < part_n && g_playlist_name_count < 1024; i++) {
			if (!part[i].d_name[0]) continue;
			const char *ext = findExt(part[i].d_name);
			if (!strstr(ext, "flac") && !strstr(ext, "mp3") && !strstr(ext, "m4a") &&
			    !strstr(ext, "mp4") && !strstr(ext, "ogg") && !strstr(ext, "wav") &&
			    !strstr(ext, "wma") && !strstr(ext, "dsf") && !strstr(ext, "dff"))
				continue;
			const char *base = strrchr(part[i].d_name, '/');
			base = base ? base + 1 : part[i].d_name;
			snprintf(g_playlist_names[g_playlist_name_count], sizeof(g_playlist_names[0]), "%s", base);
			g_playlist_name_count++;
			added++;
		}
		free(part);
	}
	int first = g_playlist_scroll_first >= 0 ? g_playlist_scroll_first : g_gui.playlist_first;
	int max_first = g_playlist_name_count > 13 ? g_playlist_name_count - 13 : 0;
	if (first > max_first) first = max_first;
	if (first < 0) first = 0;
	g_playlist_scroll_first = first;
	g_gui.playlist_first = first;
	g_gui.playlist_count = g_playlist_name_count;
	for (int i = 0; i < 13; i++) {
		int item = first + i;
		snprintf(g_gui.playlist_lines[i], sizeof(g_gui.playlist_lines[i]), "%s",
			item < g_playlist_name_count ? g_playlist_names[item] : "");
	}
	snprintf(g_gui.note, sizeof(g_gui.note), "Added %d file%s", added, added == 1 ? "" : "s");
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);

	aplay_playlist_request_reload();
	aui_set_surface_visible(AUI_PLAYLIST, 1);
}

static void aui_hide_about(void)
{
	if (g_surfaces[AUI_ABOUT].window && g_surfaces[AUI_ABOUT].visible)
		aui_set_surface_visible(AUI_ABOUT, 0);
}

static void onAboutClose(LunaElement *e)
{
	(void)e;
	aui_hide_about();
}

static void onAbout(LunaElement *e)
{
	(void)e;
	aui_hide_context_menu();
	if (!g_surfaces[AUI_ABOUT].window) return;
	int mx = 100, my = 100;
	if (g_surfaces[AUI_MAIN].window)
		glfwGetWindowPos(g_surfaces[AUI_MAIN].window, &mx, &my);
	glfwSetWindowPos(g_surfaces[AUI_ABOUT].window, mx + 40, my + 40);
	aui_set_surface_visible(AUI_ABOUT, 1);
	glfwFocusWindow(g_surfaces[AUI_ABOUT].window);
}

static void aui_hide_context_menu(void)
{
	aui_hide_device_menus();
	if (g_surfaces[AUI_MENU].window && g_surfaces[AUI_MENU].visible)
		aui_set_surface_visible(AUI_MENU, 0);
}

static void aui_set_menu_check(int idx, int on, const char *label)
{
	char buf[96];
	if (idx < 0) return;
	snprintf(buf, sizeof(buf), "%s%s", on ? "\xE2\x9C\x93 " : "", label);
	luna_set_text(idx, buf);
	/* checked class lives on the parent button when label is a child span */
	LunaElement *lbl = luna_element_at(idx);
	int btn = (lbl && lbl->parent_idx >= 0) ? lbl->parent_idx : idx;
	if (on) luna_add_class(btn, "checked");
	else luna_remove_class(btn, "checked");
}

static int g_el_mi_xtc = -1, g_el_mi_xtc_up = -1, g_el_mi_xtc_dn = -1;
static int g_el_mi_dsd = -1, g_el_mi_sr = -1, g_el_mi_loop = -1;
static int g_el_mi_skin_rand = -1, g_el_mi_eq = -1, g_el_mi_pl = -1;
static int g_el_mi_volup = -1, g_el_mi_voldn = -1;

static void aui_refresh_context_menu_labels(void)
{
	pthread_mutex_lock(&g_gui.lock);
	int xtc = g_gui.xtc_on, dsd = g_gui.dsd_on, sr = g_gui.sr_on, loop = g_gui.loop_mode;
	float vol = g_gui.volume, atten = g_gui.xtc_atten;
	pthread_mutex_unlock(&g_gui.lock);

	char buf[96];
	int vol_pct = (int)(vol * 100.0f + 0.5f);
	if (g_el_mi_volup >= 0) {
		snprintf(buf, sizeof(buf), "Volume up · %d%%", vol_pct);
		luna_set_text(g_el_mi_volup, buf);
	}
	if (g_el_mi_voldn >= 0) {
		snprintf(buf, sizeof(buf), "Volume down · %d%%", vol_pct);
		luna_set_text(g_el_mi_voldn, buf);
	}

	snprintf(buf, sizeof(buf), "Crosstalk (XTC) · %.2f", atten);
	aui_set_menu_check(g_el_mi_xtc, xtc, buf);
	if (g_el_mi_xtc_up >= 0) {
		snprintf(buf, sizeof(buf), "XTC atten + · %.2f", atten);
		luna_set_text(g_el_mi_xtc_up, buf);
	}
	if (g_el_mi_xtc_dn >= 0) {
		snprintf(buf, sizeof(buf), "XTC atten - · %.2f", atten);
		luna_set_text(g_el_mi_xtc_dn, buf);
	}

	aui_set_menu_check(g_el_mi_dsd, dsd, "DSD (DoP)");
	aui_set_menu_check(g_el_mi_sr, sr, "Super resolution");
	aui_set_menu_check(g_el_mi_loop, loop, "Repeat playlist");
	aui_set_menu_check(g_el_mi_skin_rand, g_skin_random && g_skin_pack_count > 0, "Random skin / track");
	aui_set_menu_check(g_el_mi_eq, g_surfaces[AUI_EQUALIZER].visible, "Equalizer");
	aui_set_menu_check(g_el_mi_pl, g_surfaces[AUI_PLAYLIST].visible, "Playlist editor");
	if (g_el_mi_dev >= 0) {
		pthread_mutex_lock(&g_gui.lock);
		char cur[80];
		snprintf(cur, sizeof(cur), "%s", g_gui.device[0] ? g_gui.device : "");
		pthread_mutex_unlock(&g_gui.lock);
		char buf_dev[96];
		if (cur[0])
			snprintf(buf_dev, sizeof(buf_dev), "ALSA device · %s", cur);
		else
			snprintf(buf_dev, sizeof(buf_dev), "ALSA device");
		luna_set_text(g_el_mi_dev, buf_dev);
	}
	if (g_el_mi_text >= 0) {
		snprintf(buf, sizeof(buf), "Text size · %s", g_text_scale_names[g_text_scale]);
		luna_set_text(g_el_mi_text, buf);
	}
}

static void aui_show_context_menu(AuiSurface *from, double lx, double ly)
{
	if (!g_surfaces[AUI_MENU].window) return;
	aui_refresh_context_menu_labels();
	int wx = 0, wy = 0, ww = 0, wh = 0;
	glfwGetWindowPos(from->window, &wx, &wy);
	glfwGetWindowSize(from->window, &ww, &wh);
	double sx = wx + (ww > 0 ? lx * ww / from->width : lx);
	double sy = wy + (wh > 0 ? ly * wh / from->height : ly);
	glfwSetWindowPos(g_surfaces[AUI_MENU].window, (int)lround(sx), (int)lround(sy));
	aui_set_surface_visible(AUI_MENU, 1);
	glfwFocusWindow(g_surfaces[AUI_MENU].window);
}

static void aui_apply_prepared_skin_to_ui(void)
{
	char image_css[PATH_MAX * 10];
	build_winamp_image_css(image_css, sizeof(image_css));
	luna_reset_css();
	size_t n = strlen(WINAMP_CSS) + strlen(image_css) + 1;
	char *combined = malloc(n);
	if (combined) {
		snprintf(combined, n, "%s%s", WINAMP_CSS, image_css);
		luna_parse_css(combined);
		free(combined);
	} else {
		luna_parse_css(WINAMP_CSS);
		luna_parse_css(image_css);
	}
	pthread_mutex_lock(&g_gui.lock);
	snprintf(g_gui.note, sizeof(g_gui.note), "Skin: %s", g_skin.source);
	g_gui.version++;
	pthread_mutex_unlock(&g_gui.lock);
}

static int aui_load_skin_path(const char *path, int is_builtin)
{
	int ok = is_builtin ? prepare_builtin_skin() : prepare_winamp_skin(path);
	if (!ok) return 0;
	aui_apply_prepared_skin_to_ui();
	return 1;
}

static void aui_process_pending_skin(void)
{
	if (!g_pending_skin_reload) return;
	int mode = g_pending_skin_reload;
	g_pending_skin_reload = 0;
	glfwMakeContextCurrent(g_surfaces[AUI_MAIN].window);
	if (mode == 2) {
		if (!aui_load_skin_path(NULL, 1))
			fprintf(stderr, "aplay+ui: failed to restore built-in skin\n");
		return;
	}
	if (!aui_load_skin_path(g_pending_skin_path, 0)) {
		fprintf(stderr, "aplay+ui: failed to load skin: %s\n", g_pending_skin_path);
		prepare_builtin_skin();
		aui_apply_prepared_skin_to_ui();
	}
}

static int g_el_title = -1, g_el_subtitle = -1, g_el_device = -1;
static int g_el_badge_codec = -1, g_el_badge_rate = -1, g_el_badge_state = -1;
static int g_el_progress_fill = -1, g_el_time_cur = -1, g_el_time_total = -1, g_el_track_count = -1;
static int g_el_playbtn = -1, g_el_vol_fill = -1, g_el_vol_value = -1, g_el_note = -1;
static int g_el_surface_main = -1, g_el_surface_equalizer = -1, g_el_surface_playlist = -1;
static int g_el_surface_menu = -1, g_el_surface_dev_cards = -1, g_el_surface_dev_pcms = -1;
static int g_el_surface_about = -1;
static int g_el_playlist_thumb = -1;
static int g_el_playlist_rows[13];

/* GLFW removes the native title bar for this window, so retain the familiar
 * Winamp behaviour: the 14 px skin title bar (rendered at 2x) drags the OS
 * window.  Cursor coordinates are window-relative; combining them with the
 * current window position gives a stable screen coordinate while the window
 * itself is moving. */
static void gui_cache_elements(void)
{
	g_el_surface_main  = luna_get_element_by_id("surface-main");
	g_el_surface_equalizer = luna_get_element_by_id("surface-equalizer");
	g_el_surface_playlist = luna_get_element_by_id("surface-playlist");
	g_el_surface_menu = luna_get_element_by_id("surface-menu");
	g_el_surface_dev_cards = luna_get_element_by_id("surface-dev-cards");
	g_el_surface_dev_pcms = luna_get_element_by_id("surface-dev-pcms");
	g_el_surface_about = luna_get_element_by_id("surface-about");
	g_el_playlist_thumb = luna_get_element_by_id("playlist-thumb");
	g_el_pcm_heading = luna_get_element_by_id("pcm-heading");
	g_el_mi_dev = luna_get_element_by_id("mi-dev-lbl");
	g_el_app = luna_get_element_by_id("app");
	g_el_mi_text = luna_get_element_by_id("mi-text-lbl");
	for (int i = 0; i < AUI_MAX_DEV_CARDS; i++) {
		char id[8];
		snprintf(id, sizeof(id), "dc%d", i);
		g_el_dc[i] = luna_get_element_by_id(id);
	}
	for (int i = 0; i < AUI_MAX_DEV_PCMS; i++) {
		char id[8];
		snprintf(id, sizeof(id), "dp%d", i);
		g_el_dp[i] = luna_get_element_by_id(id);
	}
	g_el_title         = luna_get_element_by_id("title");
	g_el_subtitle      = luna_get_element_by_id("subtitle");
	g_el_device        = luna_get_element_by_id("device");
	g_el_badge_codec   = luna_get_element_by_id("badge-codec");
	g_el_badge_rate    = luna_get_element_by_id("badge-rate");
	g_el_badge_state   = luna_get_element_by_id("badge-state");
	g_el_progress_fill = luna_get_element_by_id("progress-fill");
	g_el_time_cur      = luna_get_element_by_id("time-cur");
	g_el_time_total    = luna_get_element_by_id("time-total");
	g_el_track_count   = luna_get_element_by_id("track-count");
	g_el_playbtn       = luna_get_element_by_id("playbtn");
	g_el_vol_fill      = luna_get_element_by_id("vol-fill");
	g_el_vol_value     = luna_get_element_by_id("vol-value");
	g_el_note          = luna_get_element_by_id("note");
	g_el_mi_volup      = luna_get_element_by_id("mi-volup-lbl");
	g_el_mi_voldn      = luna_get_element_by_id("mi-voldn-lbl");
	g_el_mi_xtc        = luna_get_element_by_id("mi-xtc-lbl");
	g_el_mi_xtc_up     = luna_get_element_by_id("mi-xtc-up-lbl");
	g_el_mi_xtc_dn     = luna_get_element_by_id("mi-xtc-dn-lbl");
	g_el_mi_dsd        = luna_get_element_by_id("mi-dsd-lbl");
	g_el_mi_sr         = luna_get_element_by_id("mi-sr-lbl");
	g_el_mi_loop       = luna_get_element_by_id("mi-loop-lbl");
	g_el_mi_skin_rand  = luna_get_element_by_id("mi-skin-rand-lbl");
	g_el_mi_eq         = luna_get_element_by_id("mi-eq-lbl");
	g_el_mi_pl         = luna_get_element_by_id("mi-pl-lbl");
	for (int i = 0; i < 13; i++) {
		char id[8];
		snprintf(id, sizeof(id), "pl%d", i);
		g_el_playlist_rows[i] = luna_get_element_by_id(id);
	}
}

static void gui_format_time(char *out, size_t n, double seconds)
{
	if (seconds < 0) seconds = 0;
	int total = (int)(seconds + 0.5);
	snprintf(out, n, "%d:%02d", total / 60, total % 60);
}

static void gui_set_width_pct(int idx, double pct)
{
	if (idx < 0) return;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	LunaElement *e = luna_element_at(idx);
	if (!e) return;
	snprintf(e->inline_style, sizeof(e->inline_style), "width:%.2f%%;", pct);
	e->has_inline_style = 1;
	luna_update_element_style(idx);
}

static void gui_apply_state(void)
{
	static unsigned last_version = 0xFFFFFFFFu;

	pthread_mutex_lock(&g_gui.lock);
	if (g_gui.version == last_version) {
		pthread_mutex_unlock(&g_gui.lock);
		return;
	}
	GuiState snap = g_gui;
	int playlist_selected = g_playlist_selected;
	int device_picker = __atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST);
	int device_selected = g_device_picker_selected;
	last_version = g_gui.version;
	pthread_mutex_unlock(&g_gui.lock);

	char buf[192];

	luna_set_text(g_el_title, snap.filename[0] ? snap.filename : "No track loaded");
	luna_set_text(g_el_subtitle, snap.dir[0] ? snap.dir : (snap.device[0] ? snap.device : "-"));
	luna_set_text(g_el_device, snap.device);

	luna_set_text(g_el_badge_codec, snap.codec[0] ? snap.codec : "-");
	if (snap.rate > 0) {
		snprintf(buf, sizeof(buf), "%.1fkHz / %dbit / %dch", snap.rate / 1000.0, snap.bits, snap.channels);
	} else {
		snprintf(buf, sizeof(buf), "-");
	}
	luna_set_text(g_el_badge_rate, buf);
	luna_set_text(g_el_badge_state, snap.paused ? "PAUSED" : "PLAYING");

	double pct = snap.total > 0.0 ? (snap.cur / snap.total) * 100.0 : 0.0;
	gui_set_width_pct(g_el_progress_fill, pct);
	gui_format_time(buf, sizeof(buf), snap.cur);
	luna_set_text(g_el_time_cur, buf);
	gui_format_time(buf, sizeof(buf), snap.total);
	luna_set_text(g_el_time_total, buf);
	if (snap.track_total > 0) {
		snprintf(buf, sizeof(buf), "%d/%d", snap.track_index, snap.track_total);
	} else {
		buf[0] = '\0';
	}
	luna_set_text(g_el_track_count, buf);
	for (int i = 0; i < 13; i++) {
		int absolute = snap.playlist_first + i;
		if (device_picker) {
			if (snap.playlist_lines[i][0]) {
				snprintf(buf, sizeof(buf), "%d. %s", absolute + 1, snap.playlist_lines[i]);
				luna_set_text(g_el_playlist_rows[i], buf);
			} else {
				luna_set_text(g_el_playlist_rows[i], "");
			}
			const char *dev_name = aplay_device_name(absolute);
			int is_current = dev_name && snap.device[0] && !strcmp(dev_name, snap.device);
			if (is_current)
				luna_add_class(g_el_playlist_rows[i], "current");
			else
				luna_remove_class(g_el_playlist_rows[i], "current");
			if (absolute == device_selected)
				luna_add_class(g_el_playlist_rows[i], "selected");
			else
				luna_remove_class(g_el_playlist_rows[i], "selected");
		} else if (snap.playlist_lines[i][0]) {
			snprintf(buf, sizeof(buf), "%d. %s", absolute + 1, snap.playlist_lines[i]);
			luna_set_text(g_el_playlist_rows[i], buf);
			if (snap.track_index > 0 && absolute == snap.track_index - 1)
				luna_add_class(g_el_playlist_rows[i], "current");
			else
				luna_remove_class(g_el_playlist_rows[i], "current");
			if (absolute == playlist_selected)
				luna_add_class(g_el_playlist_rows[i], "selected");
			else
				luna_remove_class(g_el_playlist_rows[i], "selected");
		} else {
			luna_set_text(g_el_playlist_rows[i], "");
			luna_remove_class(g_el_playlist_rows[i], "current");
			luna_remove_class(g_el_playlist_rows[i], "selected");
		}
	}
	if (g_el_playlist_thumb >= 0) {
		int thumb_h = snap.playlist_count > 0 ? (168 * 13) / snap.playlist_count : 168;
		if (thumb_h < 12) thumb_h = 12;
		if (thumb_h > 168) thumb_h = 168;
		int max_first = snap.playlist_count > 13 ? snap.playlist_count - 13 : 0;
		int thumb_y = max_first > 0 ? (168 - thumb_h) * snap.playlist_first / max_first : 0;
		LunaElement *thumb = luna_element_at(g_el_playlist_thumb);
		if (thumb) {
			snprintf(thumb->inline_style, sizeof(thumb->inline_style), "top:%dpx;height:%dpx;", thumb_y, thumb_h);
			thumb->has_inline_style = 1;
			luna_update_element_style(g_el_playlist_thumb);
		}
	}

	luna_set_text(g_el_playbtn, "");
	gui_set_width_pct(g_el_vol_fill, snap.volume * 100.0);
	snprintf(buf, sizeof(buf), "%d%%", (int)(snap.volume * 100.0f + 0.5f));
	luna_set_text(g_el_vol_value, buf);

	luna_set_text(g_el_note, snap.note[0] ? snap.note : "\xC2\xA0");

	if (g_surfaces[AUI_MENU].visible)
		aui_refresh_context_menu_labels();

	if (snap.track_index > 0) {
		if (g_skin_last_track > 0 && snap.track_index != g_skin_last_track &&
		    g_skin_random && g_skin_pack_count > 0) {
			const char *pick = skin_pack_pick(1);
			if (pick) aui_request_skin_path(pick);
		}
		g_skin_last_track = snap.track_index;
	}
}

static void aui_logical_cursor(AuiSurface *surface, double x, double y,
	double *logical_x, double *logical_y)
{
	int ww = surface->width, wh = surface->height;
	glfwGetWindowSize(surface->window, &ww, &wh);
	*logical_x = ww > 0 ? x * surface->width / ww : x;
	*logical_y = wh > 0 ? y * surface->height / wh : y;
}

static int aui_ranges_overlap(int a0, int a1, int b0, int b1)
{
	return a0 < b1 + 1 && b0 < a1 + 1;
}

static int aui_snap_value(int value, int target, int threshold)
{
	return abs(value - target) <= threshold ? target : value;
}

static void aui_snap_window(AuiSurface *surface, int *x, int *y)
{
	const int threshold = 12;
	int ww, wh;
	glfwGetWindowSize(surface->window, &ww, &wh);

	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		AuiSurface *other = &g_surfaces[i];
		if (other == surface || !other->window || !other->visible) continue;
		int ox, oy, ow, oh;
		glfwGetWindowPos(other->window, &ox, &oy);
		glfwGetWindowSize(other->window, &ow, &oh);
		if (aui_ranges_overlap(*y, *y + wh, oy, oy + oh)) {
			*x = aui_snap_value(*x, ox + ow, threshold);
			*x = aui_snap_value(*x, ox, threshold);
			*x = aui_snap_value(*x + ww, ox, threshold) - ww;
			*x = aui_snap_value(*x + ww, ox + ow, threshold) - ww;
		}
		if (aui_ranges_overlap(*x, *x + ww, ox, ox + ow)) {
			*y = aui_snap_value(*y, oy + oh, threshold);
			*y = aui_snap_value(*y, oy, threshold);
			*y = aui_snap_value(*y + wh, oy, threshold) - wh;
			*y = aui_snap_value(*y + wh, oy + oh, threshold) - wh;
		}
	}

	/* Also snap to the usable monitor perimeter (excluding task bars/docks). */
	int cx = *x + ww / 2, cy = *y + wh / 2;
	GLFWmonitor **monitors = NULL;
	int monitor_count = 0;
	monitors = glfwGetMonitors(&monitor_count);
	for (int i = 0; i < monitor_count; i++) {
		int mx, my, mw, mh;
		glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
		if (cx < mx || cx >= mx + mw || cy < my || cy >= my + mh) continue;
		*x = aui_snap_value(*x, mx, threshold);
		*x = aui_snap_value(*x + ww, mx + mw, threshold) - ww;
		*y = aui_snap_value(*y, my, threshold);
		*y = aui_snap_value(*y + wh, my + mh, threshold) - wh;
		break;
	}
}

static void aui_cursor_pos_cb(GLFWwindow *w, double x, double y)
{
	AuiSurface *surface = glfwGetWindowUserPointer(w);
	if (!surface) return;
	g_luna_glfw_window = w;
	if (surface->resizing) {
		int wx, wy;
		glfwGetWindowPos(w, &wx, &wy);
		double screen_x = wx + x, screen_y = wy + y;
		int dw = (int)lround(screen_x - surface->resize_screen_x);
		int dh = (int)lround(screen_y - surface->resize_screen_y);
		double ratio = (double)surface->width / surface->height;
		int nw = surface->resize_width + dw;
		int nh = surface->resize_height + dh;
		if (abs(dw) >= abs(dh)) nh = (int)lround(nw / ratio);
		else nw = (int)lround(nh * ratio);
		if (nw < surface->width / 2) nw = surface->width / 2;
		if (nh < surface->height / 2) nh = surface->height / 2;
		glfwSetWindowSize(w, nw, nh);
		return;
	}
	if (surface->dragging) {
		int wx, wy;
		glfwGetWindowPos(w, &wx, &wy);
		int nx = (int)lround(wx + x - surface->drag_x);
		int ny = (int)lround(wy + y - surface->drag_y);
		aui_snap_window(surface, &nx, &ny);
		glfwSetWindowPos(w, nx, ny);
		return;
	}
	int ww, wh;
	glfwGetWindowSize(w, &ww, &wh);
	double lx, ly;
	aui_logical_cursor(surface, x, y, &lx, &ly);
	if (surface->playlist_thumb_dragging) {
		gui_scroll_playlist_to_fraction((ly - 23.0) / 168.0);
		if (g_cursor_vresize) {
			glfwSetCursor(w, g_cursor_vresize);
			g_current_cursor = -1;
		}
		return;
	}
	luna_context_mouse_move(surface->context, lx, ly);
	/* Resize corner overrides CSS cursors; invalidate luna's cache so the
	 * next non-corner move reapplies pointer/default correctly. */
	if (g_resize_cursor && x >= ww - 10.0 && y >= wh - 10.0) {
		glfwSetCursor(w, g_resize_cursor);
		g_current_cursor = -1;
	} else if (surface->kind == AUI_PLAYLIST && ly >= 0.0 && ly < 14.0 && lx < 260.0) {
		/* Title strip is used for window dragging. */
		glfwSetCursor(w, NULL);
		g_current_cursor = -1;
	}
}
static void aui_mouse_button_cb(GLFWwindow *w, int button, int action, int mods)
{
	AuiSurface *surface = glfwGetWindowUserPointer(w);
	if (!surface) return;
	g_luna_glfw_window = w;
	double x, y;
	glfwGetCursorPos(w, &x, &y);
	double lx, ly;
	aui_logical_cursor(surface, x, y, &lx, &ly);
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		if (!aui_is_popup_surface(surface->kind))
			aui_show_context_menu(surface, lx, ly);
		return;
	}
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
	    !aui_is_popup_surface(surface->kind) &&
	    (g_surfaces[AUI_MENU].visible || g_surfaces[AUI_DEV_CARDS].visible ||
	     g_surfaces[AUI_DEV_PCMS].visible)) {
		aui_hide_context_menu();
	}
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		int ww, wh;
		glfwGetWindowSize(w, &ww, &wh);
		if (action == GLFW_PRESS && !aui_is_popup_surface(surface->kind) &&
		    x >= ww - 10.0 && y >= wh - 10.0) {
			int wx, wy;
			glfwGetWindowPos(w, &wx, &wy);
			surface->resizing = 1;
			surface->resize_screen_x = wx + x;
			surface->resize_screen_y = wy + y;
			surface->resize_width = ww;
			surface->resize_height = wh;
			return;
		}
		if (surface->kind == AUI_PLAYLIST && action == GLFW_PRESS &&
		    lx >= 255.0 && lx <= 271.0 && ly >= 20.0 && ly <= 194.0) {
			surface->playlist_thumb_dragging = 1;
			gui_scroll_playlist_to_fraction((ly - 23.0) / 168.0);
			return;
		}
		/* Native Winamp title strip: the skin starts at the window's origin
		 * and is rendered at its original 1x size. */
		if (action == GLFW_PRESS && !aui_is_popup_surface(surface->kind) &&
		    ly >= 0.0 && ly < 14.0 && lx < 260.0) {
			surface->dragging = 1;
			surface->drag_x = x;
			surface->drag_y = y;
			return;
		}
		if (action == GLFW_RELEASE && (surface->dragging || surface->resizing || surface->playlist_thumb_dragging)) {
			surface->dragging = 0;
			surface->resizing = 0;
			surface->playlist_thumb_dragging = 0;
			return;
		}
		/* Direct file-list hit: bypass luna press-scale click matching. */
		if (surface->kind == AUI_PLAYLIST && action == GLFW_PRESS) {
			int row = gui_playlist_row_at(lx, ly);
			if (row >= 0) {
				gui_handle_playlist_row(row);
				return;
			}
		}
	}
	luna_context_mouse_button(surface->context, button, action, mods, lx, ly);
}
static void aui_scroll_cb(GLFWwindow *w, double xo, double yo)
{
	AuiSurface *surface = glfwGetWindowUserPointer(w);
	if (!surface) return;
	if (surface->kind == AUI_PLAYLIST && yo != 0.0)
		gui_scroll_playlist(yo > 0.0 ? -3 : 3);
	luna_context_scroll(surface->context, xo, yo);
}
static void aui_key_cb(GLFWwindow *w, int key, int sc, int act, int mods)
{
	AuiSurface *surface = glfwGetWindowUserPointer(w);
	luna_key(key, sc, act, mods);
	if (act != GLFW_PRESS && act != GLFW_REPEAT) return;
	if (surface && surface->kind == AUI_PLAYLIST) {
		int picker = __atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST);
		int current = picker ? g_device_picker_selected : g_playlist_selected;
		if (current < 0) {
			pthread_mutex_lock(&g_gui.lock);
			if (picker)
				current = g_gui.playlist_first;
			else
				current = g_gui.track_index > 0 ? g_gui.track_index - 1 : g_gui.playlist_first;
			pthread_mutex_unlock(&g_gui.lock);
		}
		int last = picker ? aplay_device_count() - 1 : g_playlist_name_count - 1;
		switch (key) {
		case GLFW_KEY_UP:        gui_select_playlist_item(current - 1, 0); return;
		case GLFW_KEY_DOWN:      gui_select_playlist_item(current + 1, 0); return;
		case GLFW_KEY_PAGE_UP:   gui_select_playlist_item(current - 13, 0); return;
		case GLFW_KEY_PAGE_DOWN: gui_select_playlist_item(current + 13, 0); return;
		case GLFW_KEY_HOME:      gui_select_playlist_item(0, 0); return;
		case GLFW_KEY_END:       gui_select_playlist_item(last, 0); return;
		case GLFW_KEY_ENTER:
		case GLFW_KEY_KP_ENTER:  gui_select_playlist_item(current, 1); return;
		case GLFW_KEY_ESCAPE:
			if (picker) { gui_set_device_picker(0); return; }
			break;
		default: break;
		}
	}
	switch (key) {
	case GLFW_KEY_SPACE: gui_inject_key(0x20); break;
	case GLFW_KEY_TAB:   gui_inject_key(0x09); break;
	case GLFW_KEY_LEFT:  gui_inject_key(KEY_LEFT); break;
	case GLFW_KEY_RIGHT: gui_inject_key(KEY_RIGHT); break;
	case GLFW_KEY_UP:    gui_inject_key(KEY_UP); break;
	case GLFW_KEY_DOWN:  gui_inject_key(KEY_DOWN); break;
	case GLFW_KEY_B:     gui_inject_key('b'); break;
	case GLFW_KEY_N:     gui_inject_key('n'); break;
	case GLFW_KEY_C:     gui_inject_key('c'); break;
	case GLFW_KEY_EQUAL:
	case GLFW_KEY_KP_ADD: gui_inject_key('+'); break;
	case GLFW_KEY_MINUS:
	case GLFW_KEY_KP_SUBTRACT: gui_inject_key('-'); break;
	case GLFW_KEY_E:     gui_inject_key('e'); break;
	case GLFW_KEY_S:     gui_inject_key('s'); break;
	case GLFW_KEY_F:     gui_inject_key('f'); break;
	case GLFW_KEY_L:     gui_inject_key('l'); break;
	case GLFW_KEY_T:
		g_text_scale = (g_text_scale + 1) % AUI_TEXT_SCALE_COUNT;
		aui_apply_text_scale();
		if (g_surfaces[AUI_MENU].visible)
			aui_refresh_context_menu_labels();
		break;
	case GLFW_KEY_D:
		if (mods & GLFW_MOD_SHIFT)
			gui_inject_key('D');
		else
			onDevicePicker(NULL);
		break;
	case GLFW_KEY_Q:
	case GLFW_KEY_ESCAPE:
		if (g_surfaces[AUI_ABOUT].visible) {
			aui_hide_about();
			break;
		}
		if (g_surfaces[AUI_MENU].visible || g_surfaces[AUI_DEV_CARDS].visible ||
		    g_surfaces[AUI_DEV_PCMS].visible) {
			aui_hide_context_menu();
			break;
		}
		if (__atomic_load_n(&g_device_picker_mode, __ATOMIC_SEQ_CST)) {
			gui_set_device_picker(0);
			break;
		}
		gui_inject_key('q'); gui_request_close(); break;
	default: break;
	}
}
static void aui_char_cb(GLFWwindow *w, unsigned int cp)             { (void)w; luna_char(cp); }
static void aui_fbsize_cb(GLFWwindow *w, int width, int height)     { (void)w; (void)width; (void)height; luna_framebuffer_resized(); }
static void aui_winsize_cb(GLFWwindow *w, int width, int height)    { (void)w; (void)width; (void)height; }
static void aui_glfw_error_cb(int error, const char *description)   { fprintf(stderr, "aplay+ui: GLFW error %d: %s\n", error, description); }

static void gui_run(void)
{
	if (g_skins_folder_arg && g_skins_folder_arg[0]) {
		if (scan_skins_folder(g_skins_folder_arg) > 0) {
			snprintf(g_skins_folder, sizeof(g_skins_folder), "%s", g_skins_folder_arg);
			g_skin_random = 1;
			xoroshiro128plus_init((uint64_t)time(NULL) ^ ((uint64_t)getpid() << 16));
			fprintf(stderr, "aplay+ui: skin pack (%d): %s\n", g_skin_pack_count, g_skins_folder);
			if (!g_skin_arg) {
				const char *pick = skin_pack_pick(0);
				if (pick) g_skin_arg = pick;
			}
		} else {
			fprintf(stderr, "aplay+ui: no skins found in pack folder: %s\n", g_skins_folder_arg);
		}
	}
	if (g_skin_arg) {
		if (prepare_winamp_skin(g_skin_arg)) {
			fprintf(stderr, "aplay+ui: using Winamp skin: %s\n", g_skin.source[0] ? g_skin.source : g_skin_arg);
		} else {
			fprintf(stderr, "aplay+ui: falling back to the built-in skin\n");
			cleanup_winamp_skin();
			prepare_builtin_skin();
		}
	} else {
		prepare_builtin_skin();
	}
	if (!g_skin.enabled) {
		fprintf(stderr, "aplay+ui: no usable skin; GUI disabled\n");
		return;
	}
	glfwSetErrorCallback(aui_glfw_error_cb);
	if (!glfwInit()) {
		fprintf(stderr, "aplay+ui: glfwInit() failed, running headless (GUI disabled)\n");
		return;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

	memset(g_surfaces, 0, sizeof(g_surfaces));
	g_surfaces[AUI_MAIN] = (AuiSurface){ .kind=AUI_MAIN, .width=275, .height=116, .origin_x=0, .origin_y=0, .visible=1 };
	g_surfaces[AUI_EQUALIZER] = (AuiSurface){ .kind=AUI_EQUALIZER, .width=275, .height=116, .origin_x=300, .origin_y=0, .visible=1 };
	g_surfaces[AUI_PLAYLIST] = (AuiSurface){ .kind=AUI_PLAYLIST, .width=275, .height=232, .origin_x=600, .origin_y=0, .visible=1 };
	g_surfaces[AUI_MENU] = (AuiSurface){ .kind=AUI_MENU, .width=AUI_MENU_W, .height=AUI_MENU_H, .origin_x=0, .origin_y=250, .visible=0 };
	g_surfaces[AUI_DEV_CARDS] = (AuiSurface){ .kind=AUI_DEV_CARDS, .width=240, .height=280, .origin_x=302, .origin_y=250, .visible=0 };
	g_surfaces[AUI_DEV_PCMS] = (AuiSurface){ .kind=AUI_DEV_PCMS, .width=240, .height=280, .origin_x=552, .origin_y=250, .visible=0 };
	g_surfaces[AUI_ABOUT] = (AuiSurface){ .kind=AUI_ABOUT, .width=340, .height=380, .origin_x=520, .origin_y=680, .visible=0 };
	const char *titles[AUI_SURFACE_COUNT] = {
		"aplay+ — player",
		"aplay+ — equalizer",
		"aplay+ — playlist",
		"aplay+ — menu",
		"aplay+ — sound cards",
		"aplay+ — devices",
		"aplay+ — about"
	};
	GLFWwindow *share = NULL;
	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		if (aui_is_popup_surface(i)) {
			glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
			glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
		} else {
			glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		}
		g_surfaces[i].window = glfwCreateWindow(g_surfaces[i].width, g_surfaces[i].height, titles[i], NULL, share);
		if (!g_surfaces[i].window) break;
		if (!share) share = g_surfaces[i].window;
	}
	if (!g_surfaces[AUI_MAIN].window || !g_surfaces[AUI_EQUALIZER].window ||
	    !g_surfaces[AUI_PLAYLIST].window || !g_surfaces[AUI_MENU].window ||
	    !g_surfaces[AUI_DEV_CARDS].window || !g_surfaces[AUI_DEV_PCMS].window ||
	    !g_surfaces[AUI_ABOUT].window) {
		fprintf(stderr, "aplay+ui: glfwCreateWindow() failed, running headless (GUI disabled)\n");
		for (int i = AUI_SURFACE_COUNT - 1; i >= 0; i--)
			if (g_surfaces[i].window) glfwDestroyWindow(g_surfaces[i].window);
		glfwTerminate();
		return;
	}
	g_luna_glfw_window = g_surfaces[AUI_MAIN].window;
	glfwMakeContextCurrent(g_surfaces[AUI_MAIN].window);
	glfwSwapInterval(1);
	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		GLFWwindow *w = g_surfaces[i].window;
		glfwSetWindowUserPointer(w, &g_surfaces[i]);
		if (!aui_is_popup_surface(i)) {
			glfwSetWindowSizeLimits(w, g_surfaces[i].width / 2, g_surfaces[i].height / 2,
				GLFW_DONT_CARE, GLFW_DONT_CARE);
			glfwSetWindowAspectRatio(w, g_surfaces[i].width, g_surfaces[i].height);
		}
		glfwSetCursorPosCallback(w, aui_cursor_pos_cb);
		glfwSetMouseButtonCallback(w, aui_mouse_button_cb);
		glfwSetScrollCallback(w, aui_scroll_cb);
		glfwSetKeyCallback(w, aui_key_cb);
		glfwSetCharCallback(w, aui_char_cb);
		glfwSetFramebufferSizeCallback(w, aui_fbsize_cb);
		glfwSetWindowSizeCallback(w, aui_winsize_cb);
	}

	LunaPlatform plat = {0};
	plat.get_time = glfwGetTime;
	plat.get_proc = (LunaGetProcFn)glfwGetProcAddress;
	luna_set_platform(&plat);

	LunaInitConfig cfg = {0};
	cfg.width = 875.0f;
	cfg.height = 800.0f;
	cfg.get_proc = (LunaGetProcFn)glfwGetProcAddress;
	cfg.frameless = 0;
	if (!luna_init(&cfg)) {
		fprintf(stderr, "aplay+ui: luna_init() failed, running headless (GUI disabled)\n");
		for (int i = AUI_SURFACE_COUNT - 1; i >= 0; i--) glfwDestroyWindow(g_surfaces[i].window);
		glfwTerminate();
		return;
	}

	luna_register_js_handler("onPrev", onPrev);
	luna_register_js_handler("onNext", onNext);
	luna_register_js_handler("onPlayPause", onPlayPause);
	luna_register_js_handler("onStop", onStop);
	luna_register_js_handler("onRew", onRew);
	luna_register_js_handler("onFwd", onFwd);
	luna_register_js_handler("onVolUp", onVolUp);
	luna_register_js_handler("onVolDown", onVolDown);
	luna_register_js_handler("onXtc", onXtc);
	luna_register_js_handler("onXtcUp", onXtcUp);
	luna_register_js_handler("onXtcDown", onXtcDown);
	luna_register_js_handler("onDsd", onDsd);
	luna_register_js_handler("onSr", onSr);
	luna_register_js_handler("onFmt", onFmt);
	luna_register_js_handler("onLoop", onLoop);
	luna_register_js_handler("onQuit", onQuit);
	luna_register_js_handler("onDevicePicker", onDevicePicker);
	luna_register_js_handler("onToggleEq", onToggleEq);
	luna_register_js_handler("onTogglePlaylist", onTogglePlaylist);
	luna_register_js_handler("onHideEq", onHideEq);
	luna_register_js_handler("onHidePlaylist", onHidePlaylist);
	luna_register_js_handler("onPlaylistItem", onPlaylistItem);
	luna_register_js_handler("onMenuPlayPause", onMenuPlayPause);
	luna_register_js_handler("onMenuStop", onMenuStop);
	luna_register_js_handler("onMenuPrev", onMenuPrev);
	luna_register_js_handler("onMenuNext", onMenuNext);
	luna_register_js_handler("onMenuRew", onMenuRew);
	luna_register_js_handler("onMenuFwd", onMenuFwd);
	luna_register_js_handler("onMenuVolUp", onMenuVolUp);
	luna_register_js_handler("onMenuVolDown", onMenuVolDown);
	luna_register_js_handler("onMenuToggleEq", onMenuToggleEq);
	luna_register_js_handler("onMenuTogglePlaylist", onMenuTogglePlaylist);
	luna_register_js_handler("onMenuXtc", onMenuXtc);
	luna_register_js_handler("onMenuXtcUp", onMenuXtcUp);
	luna_register_js_handler("onMenuXtcDown", onMenuXtcDown);
	luna_register_js_handler("onMenuDsd", onMenuDsd);
	luna_register_js_handler("onMenuSr", onMenuSr);
	luna_register_js_handler("onMenuLoop", onMenuLoop);
	luna_register_js_handler("onMenuFmt", onMenuFmt);
	luna_register_js_handler("onMenuDevice", onMenuDevice);
	luna_register_js_handler("onMenuQuit", onMenuQuit);
	luna_register_js_handler("onMenuTextSize", onMenuTextSize);
	luna_register_js_handler("onDevCard", onDevCard);
	luna_register_js_handler("onDevPcm", onDevPcm);
	luna_register_js_handler("onSkinRandom", onSkinRandom);
	luna_register_js_handler("onSkinNext", onSkinNext);
	luna_register_js_handler("onSkinFolder", onSkinFolder);
	luna_register_js_handler("onSkinBuiltin", onSkinBuiltin);
	luna_register_js_handler("onAddFolder", onAddFolder);
	luna_register_js_handler("onAbout", onAbout);
	luna_register_js_handler("onAboutClose", onAboutClose);

	char image_css[PATH_MAX * 10];
	build_winamp_image_css(image_css, sizeof(image_css));
	/* luna resolves var() per stylesheet. Skin colors live in image_css while
	 * the rules that consume them live in WINAMP_CSS, so parse them as one. */
	{
		size_t n = strlen(WINAMP_CSS) + strlen(image_css) + 1;
		char *combined = malloc(n);
		if (combined) {
			snprintf(combined, n, "%s%s", WINAMP_CSS, image_css);
			luna_parse_css(combined);
			free(combined);
		} else {
			luna_parse_css(WINAMP_CSS);
			luna_parse_css(image_css);
		}
	}
	luna_parse_html(WINAMP_HTML);
	luna_inject_body_background();
	luna_wire_onclick_handlers();
	gui_cache_elements();
	const int roots[AUI_SURFACE_COUNT] = {
		g_el_surface_main, g_el_surface_equalizer, g_el_surface_playlist,
		g_el_surface_menu, g_el_surface_dev_cards, g_el_surface_dev_pcms,
		g_el_surface_about
	};
	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		glfwMakeContextCurrent(g_surfaces[i].window);
		glfwSwapInterval(i == AUI_MAIN ? 1 : 0);
		g_surfaces[i].context = luna_context_create();
		if (g_surfaces[i].context)
			luna_context_set_region(g_surfaces[i].context, roots[i],
				g_surfaces[i].origin_x, g_surfaces[i].origin_y,
				(float)g_surfaces[i].width, (float)g_surfaces[i].height);
	}
	if (!g_surfaces[AUI_MAIN].context || !g_surfaces[AUI_EQUALIZER].context ||
	    !g_surfaces[AUI_PLAYLIST].context || !g_surfaces[AUI_MENU].context ||
	    !g_surfaces[AUI_DEV_CARDS].context || !g_surfaces[AUI_DEV_PCMS].context ||
	    !g_surfaces[AUI_ABOUT].context) {
		fprintf(stderr, "aplay+ui: failed to create Luna surface contexts\n");
		gui_request_close();
	}
	glfwSetWindowPos(g_surfaces[AUI_MAIN].window, 100, 100);
	glfwSetWindowPos(g_surfaces[AUI_EQUALIZER].window, 100, 216);
	glfwSetWindowPos(g_surfaces[AUI_PLAYLIST].window, 100, 332);
#ifdef GLFW_RESIZE_NWSE_CURSOR
  g_resize_cursor = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
#else
  g_resize_cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#endif
	g_hand_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
	g_cursor_ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
	g_cursor_crosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
	g_cursor_hresize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
	g_cursor_vresize = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		if (aui_is_popup_surface(i)) continue;
		glfwShowWindow(g_surfaces[i].window);
	}

	double last_time = glfwGetTime();
	while (!__atomic_load_n(&g_gui_should_close, __ATOMIC_SEQ_CST)) {
		glfwPollEvents();
		if (glfwWindowShouldClose(g_surfaces[AUI_MAIN].window)) {
			gui_request_close();
			break;
		}
		for (int i = AUI_EQUALIZER; i < AUI_SURFACE_COUNT; i++) {
			if (glfwWindowShouldClose(g_surfaces[i].window))
				aui_set_surface_visible(i, 0);
		}

		double now = glfwGetTime();
		double dt = now - last_time;
		last_time = now;

		gui_apply_state();
		aui_process_pending_skin();
		luna_update(now, dt);

		for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
			AuiSurface *surface = &g_surfaces[i];
			if (!surface->visible || !surface->context) continue;
			g_luna_glfw_window = surface->window;
			glfwMakeContextCurrent(surface->window);
			int fbw, fbh;
			glfwGetFramebufferSize(surface->window, &fbw, &fbh);
			glViewport(0, 0, fbw, fbh);
			luna_context_render(surface->context, fbw, fbh);
			glfwSwapBuffers(surface->window);
		}
	}

	for (int i = 0; i < AUI_SURFACE_COUNT; i++) {
		glfwMakeContextCurrent(g_surfaces[i].window);
		luna_context_destroy(g_surfaces[i].context);
		glfwDestroyWindow(g_surfaces[i].window);
	}
	if (g_resize_cursor) glfwDestroyCursor(g_resize_cursor);
	if (g_hand_cursor) glfwDestroyCursor(g_hand_cursor);
	if (g_cursor_ibeam) glfwDestroyCursor(g_cursor_ibeam);
	if (g_cursor_crosshair) glfwDestroyCursor(g_cursor_crosshair);
	if (g_cursor_hresize) glfwDestroyCursor(g_cursor_hresize);
	if (g_cursor_vresize) glfwDestroyCursor(g_cursor_vresize);
	g_resize_cursor = NULL;
	g_hand_cursor = g_cursor_ibeam = g_cursor_crosshair = NULL;
	g_cursor_hresize = g_cursor_vresize = NULL;
	glfwTerminate();
}

typedef struct {
	int flag;
	char *dir;
	char *type;
	char *regexp;
} PlaybackArgs;

static int consume_long_skin_option(int argc, char **argv)
{
	int out = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--skin")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "aplay+ui: --skin requires a .wsz file or directory\n");
				return -1;
			}
			g_skin_arg = argv[++i];
		} else if (!strncmp(argv[i], "--skin=", 7)) {
			if (!argv[i][7]) {
				fprintf(stderr, "aplay+ui: --skin requires a .wsz file or directory\n");
				return -1;
			}
			g_skin_arg = argv[i] + 7;
		} else if (!strcmp(argv[i], "--skins")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "aplay+ui: --skins requires a directory of Winamp skins\n");
				return -1;
			}
			g_skins_folder_arg = argv[++i];
		} else if (!strncmp(argv[i], "--skins=", 8)) {
			if (!argv[i][8]) {
				fprintf(stderr, "aplay+ui: --skins requires a directory of Winamp skins\n");
				return -1;
			}
			g_skins_folder_arg = argv[i] + 8;
		} else {
			argv[out++] = argv[i];
		}
	}
	argv[out] = NULL;
	return out;
}

/* The playlist scanner changes the process working directory. Resolve the
 * skin while main still has the caller's original working directory. */
static void freeze_abs_path(const char *arg, char *out, size_t out_n)
{
	if (!arg || !arg[0] || !out || out_n == 0) return;
	char resolved[PATH_MAX];
	if (realpath(arg, resolved)) {
		snprintf(out, out_n, "%s", resolved);
	} else if (arg[0] == '/') {
		snprintf(out, out_n, "%s", arg);
	} else {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd)) &&
		    snprintf(out, out_n, "%s/%s", cwd, arg) < (int)out_n) {
			/* Keep the absolute attempted path so diagnostics name the right file. */
		} else {
			snprintf(out, out_n, "%s", arg);
		}
	}
}

static void freeze_skin_path(void)
{
	if (g_skin_arg && g_skin_arg[0]) {
		freeze_abs_path(g_skin_arg, g_skin_path, sizeof(g_skin_path));
		g_skin_arg = g_skin_path;
	}
	if (g_skins_folder_arg && g_skins_folder_arg[0]) {
		static char skins_abs[PATH_MAX];
		freeze_abs_path(g_skins_folder_arg, skins_abs, sizeof(skins_abs));
		g_skins_folder_arg = skins_abs;
	}
}

static void *audio_engine_thread(void *arg)
{
	PlaybackArgs *pa = (PlaybackArgs *)arg;
	if (pa->flag & USE_TEST_MODE) {
		int format = pa->flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;
		play_test_mode(format, pa->flag);
	} else {
		play_dir(pa->dir, pa->type, pa->regexp, pa->flag);
	}
	gui_request_close();
	return NULL;
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
	int device_specified = 0;
	argc = consume_long_skin_option(argc, argv);
	if (argc < 0) return 1;

	gui_bridge_init();
	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hS:R:d:frxs:t:pclvDT:V:eo:")) != -1) {
		switch (c) {
		case 1:  dir = (char*)ps.optarg; break;
		case 'S': g_skin_arg = ps.optarg; break;
		case 'R': g_skins_folder_arg = ps.optarg; break;
		case 'd': aplay_copy_dev(ps.optarg); device_specified = 1; break;
		case 'f': flag |= USE_FLOAT32; break;
		case 'r': flag |= LS_RECURSIVE; break;
		case 'x': flag |= LS_RANDOM; break;
		case 's':
			regexp = (char*)ps.optarg;
			printf("Search with '%s'.\n", regexp);
			break;
		case 't': type = (char*)ps.optarg; break;
		case 'p': {
			FILE *fp = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "w");
			fprintf(fp, "tsc");
			fclose(fp);
			set_realtime_priority();
			set_cpu("performance");
			clock = 1;
			break;
		}
		case 'c':
			flag |= USE_CROSSTALK;
			printf("Crosstalk cancellation enabled.\n");
			break;
		case 'T':
			flag |= USE_TEST_MODE;
			printf("Test mode enabled.\n");
			break;
		case 'e':
			flag |= USE_DSD_ENCODE;
			printf("Real-time PCM->DSD64 (DoP) encoding enabled.\n");
			break;
		case 'o': dsd_file_path = (char*)ps.optarg; break;
		case 'l': loop_mode = 1; break;
		case 'v': verbose = 1; break;
		case 'V':
			volume = atof(ps.optarg);
			if (volume < 0.0f || volume > 1.0f) volume = 1.0f;
			break;
		case 'D': speaker_distance_m = atof(ps.optarg); break;
		case 'h':
			usage(stderr, argc, argv);
			list_alsa_devices();
			return 1;
		}
	}
	freeze_skin_path();

	if (!device_specified)
		aplay_auto_select_device();

	apply_alsa_volume();

	if (dsd_file_path) {
		g_dsd_raw_file = fopen(dsd_file_path, "wb");
		if (!g_dsd_raw_file) {
			perror("fopen (-o DSD output file)");
		} else {
			printf("DSD raw bitstream will also be written to: %s\n", dsd_file_path);
		}
	}

	g_start_flag = flag;
	PlaybackArgs pargs = { .flag = flag, .dir = dir, .type = type, .regexp = regexp };
	pthread_t audio_tid;
	if (pthread_create(&audio_tid, NULL, audio_engine_thread, &pargs) != 0) {
		fprintf(stderr, "aplay+ui: failed to start audio engine thread, running without a GUI.\n");
		audio_engine_thread(&pargs);
	} else {
		gui_run();
		gui_inject_key('q');
		pthread_join(audio_tid, NULL);
	}

	if (g_dsd_raw_file) {
		fclose(g_dsd_raw_file);
	}
	cleanup_winamp_skin();

	if (clock) {
		set_cpu("ondemand");
	}

	return 0;
}
