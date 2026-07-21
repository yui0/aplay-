/* public domain Simple, Minimalistic, ANSI TUI panel for aplay+
 *
 * Draws a fixed-height "now playing" panel using plain ANSI escape
 * sequences (no ncurses dependency, matches the style of kbhit.h / ls.h).
 * Call tui_open() once before playback starts, tui_render() every time
 * you want to refresh the panel (e.g. once per decoded chunk, throttled),
 * and tui_close() when playback of the whole session ends.
 *
 * Usage:
 *	tui_open();
 *	tui_state_t s = {0};
 *	s.filename = "song.flac"; s.codec = "FLAC"; ...
 *	tui_render(&s);
 *	...
 *	tui_close();
 * */
#ifndef TUI_H
#define TUI_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TUI_MIN_WIDTH	60
#define TUI_MAX_WIDTH	110
#define TUI_LINES	8	/* top border + 6 content lines + bottom border */

typedef struct {
	int track_index;	/* 1-based current track number (0 = unknown) */
	int track_total;	/* total tracks in playlist (0 = unknown) */
	const char *filename;	/* path/name of the current file */
	const char *codec;	/* e.g. "FLAC", "MP3", "AAC" ... */
	int rate;		/* sample rate in Hz */
	int bits;		/* bit depth (0 if not applicable/unknown) */
	int channels;
	const char *device;	/* ALSA device name */

	int paused;

	int use_time;		/* 1: cur/total are seconds, 0: cur_raw/total_raw are raw units */
	double cur;		/* elapsed seconds (use_time==1) */
	double total;		/* total seconds (use_time==1), 0 if unknown */
	uint64_t cur_raw;	/* elapsed raw units (use_time==0) */
	uint64_t total_raw;	/* total raw units (use_time==0), 0 if unknown */
	const char *unit;	/* label for raw units, e.g. "bytes" */

	float volume;		/* 0.0 - 1.0 */
	int xtc_on;
	float xtc_atten;
	int loop_mode;

	const char *note;	/* optional short transient note, e.g. "buffer underrun", NULL if none */
} tui_state_t;

static int tui_width = TUI_MIN_WIDTH;
static int tui_drawn = 0;	/* have we already drawn the panel at least once? */

static int tui_get_width(void)
{
	struct winsize w;
	int cols = TUI_MIN_WIDTH;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
		cols = w.ws_col;
	}
	if (cols < TUI_MIN_WIDTH) {
		cols = TUI_MIN_WIDTH;
	}
	if (cols > TUI_MAX_WIDTH) {
		cols = TUI_MAX_WIDTH;
	}
	return cols;
}

/* Print `s` truncated/padded to exactly `w` display columns, wrapped by
 * a leading/trailing vertical bar. Assumes single-byte-per-column text
 * (ASCII); multi-byte UTF-8 file names may render slightly misaligned,
 * which is an accepted tradeoff for keeping this header dependency-free. */
static void tui_line(const char *s, int w)
{
	int len = (int)strlen(s);
	printf("\r\e[K\xe2\x94\x82"); /* │ */
	if (len >= w) {
		fwrite(s, 1, w, stdout);
	} else {
		fputs(s, stdout);
		for (int i = len; i < w; i++) {
			fputc(' ', stdout);
		}
	}
	printf("\xe2\x94\x82\n"); /* │ */
}

static void tui_hline(const char *left, const char *mid, const char *right, int w)
{
	printf("\r\e[K%s", left);
	for (int i = 0; i < w; i++) {
		fputs(mid, stdout);
	}
	printf("%s\n", right);
}

static void tui_open(void)
{
	tui_width = tui_get_width();
	tui_drawn = 0;
	printf("\e[?25l"); /* hide cursor */
	fflush(stdout);
}

static void tui_close(void)
{
	printf("\e[?25h\n"); /* show cursor, drop below the panel */
	fflush(stdout);
	tui_drawn = 0;
}

/* Build a "[####------]" bar of interior width `bw` given a 0..1 ratio.
 * ratio < 0 means "unknown length" -> render a hint instead of a bar. */
static void tui_bar(char *out, int outsz, int bw, double ratio)
{
	if (bw < 4) {
		bw = 4;
	}
	if (bw > outsz - 3) {
		bw = outsz - 3;
	}
	int filled = 0;
	int unknown = ratio < 0.0;
	if (!unknown) {
		if (ratio < 0.0) {
			ratio = 0.0;
		}
		if (ratio > 1.0) {
			ratio = 1.0;
		}
		filled = (int)(ratio * bw);
	}
	int p = 0;
	out[p++] = '[';
	for (int i = 0; i < bw; i++) {
		if (unknown) {
			out[p++] = '-';
		} else {
			out[p++] = (i < filled) ? '#' : '-';
		}
	}
	out[p++] = ']';
	out[p] = 0;
}

static void tui_render(tui_state_t *s)
{
	tui_width = tui_get_width();
	int inner = tui_width - 2; /* usable columns between the two borders */
	char line[512];
	char bar[256];

	if (tui_drawn) {
		printf("\e[%dA", TUI_LINES); /* move cursor back to top of panel */
	}

	tui_hline("\xe2\x94\x8c", "\xe2\x94\x80", "\xe2\x94\x90", inner); /* ┌──┐ */

	/* line 1: title + playlist position */
	if (s->track_total > 0) {
		snprintf(line, sizeof(line), " aplay+   [%d/%d]%s",
		         s->track_index, s->track_total,
		         s->paused ? "   \xe2\x8f\xb8 PAUSED" : "");
	} else {
		snprintf(line, sizeof(line), " aplay+ %s",
		         s->paused ? "   \xe2\x8f\xb8 PAUSED" : "");
	}
	tui_line(line, inner);

	/* line 2: filename */
	snprintf(line, sizeof(line), " %s", s->filename ? s->filename : "");
	tui_line(line, inner);

	/* line 3: format info */
	if (s->bits > 0) {
		snprintf(line, sizeof(line), " %-5s %dHz %dbit %dch  dev:%s",
		         s->codec ? s->codec : "", s->rate, s->bits, s->channels,
		         s->device ? s->device : "");
	} else {
		snprintf(line, sizeof(line), " %-5s %dHz %dch  dev:%s",
		         s->codec ? s->codec : "", s->rate, s->channels,
		         s->device ? s->device : "");
	}
	tui_line(line, inner);

	/* line 4: progress bar */
	{
		int barw = inner - 24;
		if (s->use_time) {
			double ratio = (s->total > 0.0) ? (s->cur / s->total) : -1.0;
			tui_bar(bar, sizeof(bar), barw, ratio);
			int cm = (int)(s->cur) / 60, cs = (int)(s->cur) % 60;
			int tm = (int)(s->total) / 60, ts = (int)(s->total) % 60;
			if (s->total > 0.0) {
				snprintf(line, sizeof(line), " %02d:%02d %s %02d:%02d",
				         cm, cs, bar, tm, ts);
			} else {
				snprintf(line, sizeof(line), " %02d:%02d %s --:--",
				         cm, cs, bar);
			}
		} else {
			double ratio = (s->total_raw > 0) ?
			               ((double)s->cur_raw / (double)s->total_raw) : -1.0;
			tui_bar(bar, sizeof(bar), barw, ratio);
			if (s->total_raw > 0) {
				snprintf(line, sizeof(line), " %llu %s %llu %s",
				         (unsigned long long)s->cur_raw, bar,
				         (unsigned long long)s->total_raw,
				         s->unit ? s->unit : "");
			} else {
				snprintf(line, sizeof(line), " %llu %s %s",
				         (unsigned long long)s->cur_raw, bar,
				         s->unit ? s->unit : "");
			}
		}
	}
	tui_line(line, inner);

	/* line 5: volume / crosstalk / loop, plus any transient note */
	if (s->xtc_on) {
		snprintf(line, sizeof(line), " Vol:%3.0f%%  XTC:ON(%.2f)  Loop:%s%s%s",
		         s->volume * 100.0f, s->xtc_atten, s->loop_mode ? "ON" : "OFF",
		         s->note ? "  " : "", s->note ? s->note : "");
	} else {
		snprintf(line, sizeof(line), " Vol:%3.0f%%  XTC:OFF  Loop:%s%s%s",
		         s->volume * 100.0f, s->loop_mode ? "ON" : "OFF",
		         s->note ? "  " : "", s->note ? s->note : "");
	}
	tui_line(line, inner);

	/* line 6: keybind footer */
	tui_line(" [Space]Pause [C]rosstalk [B]ack [Q/Esc]Quit  (any other key: next)", inner);

	tui_hline("\xe2\x94\x94", "\xe2\x94\x80", "\xe2\x94\x98", inner); /* └──┘ */

	fflush(stdout);
	tui_drawn = 1;
}

#endif
