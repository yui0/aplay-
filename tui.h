/* public domain Simple, Minimalistic, ANSI TUI panel for aplay+
 *	©2026 Yuichiro Nakada
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
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TUI_MIN_WIDTH	60
#define TUI_MAX_WIDTH	110
#define TUI_LINES	8	/* top border + 6 content lines + bottom border */
#define TUI_MARQUEE_CPS	6.0	/* telop scroll speed in characters per second */

/* ---- colors -------------------------------------------------------
 * Plain SGR escapes, 256-color palette. Every color-wrapped string that
 * flows through tui_line() keeps correct column alignment because
 * tui_line() measures/copies "visible" columns and passes escape bytes
 * through untouched (see tui_visible_len / tui_line below). */
#define C_RESET		"\e[0m"
#define C_DIM		"\e[2m"
#define C_BOLD		"\e[1m"
#define C_BORDER	"\e[38;5;73m"		/* steel teal frame */
#define C_TITLE		"\e[1;38;5;213m"	/* bold pink/magenta */
#define C_NOTE_ICON	"\e[38;5;220m"		/* gold note glyph */
#define C_FILE		"\e[1;38;5;123m"	/* bold light cyan - "telop" */
#define C_CODEC		"\e[1;38;5;135m"	/* bold purple */
#define C_LABEL		"\e[38;5;250m"		/* light gray */
#define C_DEVICE	"\e[38;5;244m"		/* dim gray */
#define C_BAR_FILL	"\e[38;5;42m"		/* teal-green fill */
#define C_BAR_EMPTY	"\e[38;5;238m"		/* dark gray track */
#define C_BAR_UNKNOWN	"\e[38;5;244m"
#define C_TIME		"\e[38;5;250m"
#define C_PAUSE		"\e[1;38;5;196m"	/* bold red badge */
#define C_ON		"\e[1;38;5;46m"		/* bold green */
#define C_OFF		"\e[38;5;240m"		/* dim gray */
#define C_VOL_HI	"\e[1;38;5;46m"		/* bold green  (>66%) */
#define C_VOL_MID	"\e[1;38;5;220m"	/* bold yellow (33-66%) */
#define C_VOL_LO	"\e[1;38;5;203m"	/* bold orange/red (<33%) */
#define C_NOTEMSG	"\e[1;38;5;226m"	/* bold yellow transient note */
#define C_FOOTER	"\e[38;5;244m"

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

/* Visible-column length of `s`, skipping over any "\e[...m" SGR escape
 * sequences so color codes never throw off panel alignment. */
static int tui_visible_len(const char *s)
{
	int len = 0;
	while (*s) {
		if (s[0] == '\e' && s[1] == '[') {
			s += 2;
			while (*s && *s != 'm') {
				s++;
			}
			if (*s) {
				s++;
			}
		} else {
			len++;
			s++;
		}
	}
	return len;
}

/* Print `s` truncated/padded to exactly `w` *visible* columns, wrapped by
 * a leading/trailing vertical bar. Embedded "\e[...m" escapes in `s` are
 * copied through verbatim and don't count against `w`. Assumes
 * single-byte-per-column text otherwise (ASCII); multi-byte UTF-8 file
 * names may render slightly misaligned, an accepted tradeoff for keeping
 * this header dependency-free. */
static void tui_line(const char *s, int w)
{
	int len = tui_visible_len(s);
	printf("\r\e[K" C_BORDER "\xe2\x94\x82" C_RESET); /* │ */
	if (len >= w) {
		int printed = 0;
		const char *p = s;
		while (*p && printed < w) {
			if (p[0] == '\e' && p[1] == '[') {
				const char *start = p;
				p += 2;
				while (*p && *p != 'm') {
					p++;
				}
				if (*p) {
					p++;
				}
				fwrite(start, 1, (size_t)(p - start), stdout);
			} else {
				fputc(*p, stdout);
				p++;
				printed++;
			}
		}
		fputs(C_RESET, stdout); /* guard against a color left open by truncation */
	} else {
		fputs(s, stdout);
		for (int i = len; i < w; i++) {
			fputc(' ', stdout);
		}
	}
	printf(C_BORDER "\xe2\x94\x82" C_RESET "\n"); /* │ */
}

static void tui_hline(const char *left, const char *mid, const char *right, int w)
{
	printf("\r\e[K" C_BORDER "%s", left);
	for (int i = 0; i < w; i++) {
		fputs(mid, stdout);
	}
	printf("%s" C_RESET "\n", right);
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
	printf(C_RESET "\e[?25h\n"); /* reset color, show cursor, drop below the panel */
	fflush(stdout);
	tui_drawn = 0;
}

/* Build a "[####------]" bar of interior width `bw` given a 0..1 ratio,
 * with the filled/empty portions individually colorized.
 * ratio < 0 means "unknown length" -> render a hint instead of a bar. */
static void tui_bar(char *out, int outsz, int bw, double ratio)
{
	int reserve = 48; /* room for the ANSI color escapes we splice in */
	if (bw < 4) {
		bw = 4;
	}
	if (bw > outsz - reserve) {
		bw = outsz - reserve;
	}
	if (bw < 4) {
		bw = 4;
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
	if (unknown) {
		p += sprintf(out + p, "%s", C_BAR_UNKNOWN);
		for (int i = 0; i < bw; i++) {
			out[p++] = '-';
		}
		p += sprintf(out + p, "%s", C_RESET);
	} else {
		p += sprintf(out + p, "%s", C_BAR_FILL);
		for (int i = 0; i < filled; i++) {
			out[p++] = '#';
		}
		p += sprintf(out + p, "%s", C_BAR_EMPTY);
		for (int i = filled; i < bw; i++) {
			out[p++] = '-';
		}
		p += sprintf(out + p, "%s", C_RESET);
	}
	out[p++] = ']';
	out[p] = 0;
}

/* Scrolling "telop" for filenames that don't fit the panel width.
 * Advances by wall-clock time (TUI_MARQUEE_CPS chars/sec) so speed is
 * independent of filename length and of how often tui_render() is called. */
static char tui_marquee_last[512] = {0};
static int tui_marquee_pos = 0;
static struct timespec tui_marquee_t0 = {0, 0};

static void tui_marquee(const char *filename, int width, char *out, int outsz)
{
	if (!filename) {
		filename = "(unknown)";
	}
	if (strcmp(filename, tui_marquee_last) != 0) {
		snprintf(tui_marquee_last, sizeof(tui_marquee_last), "%.500s", filename);
		tui_marquee_pos = 0;
		clock_gettime(CLOCK_MONOTONIC, &tui_marquee_t0);
	}
	if (width <= 0) {
		out[0] = 0;
		return;
	}
	int flen = (int)strlen(filename);
	if (flen <= width) {
		snprintf(out, (size_t)outsz, "%s", filename);
		return;
	}
	char loop[600];
	snprintf(loop, sizeof(loop), "%s   *   ", filename);
	int looplen = (int)strlen(loop);
	if (looplen <= 0) {
		out[0] = 0;
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	double elapsed = (double)(now.tv_sec - tui_marquee_t0.tv_sec) +
	                 (double)(now.tv_nsec - tui_marquee_t0.tv_nsec) * 1e-9;
	if (elapsed < 0.0) {
		elapsed = 0.0;
	}
	tui_marquee_pos = (int)(elapsed * (double)TUI_MARQUEE_CPS) % looplen;

	int cnt = width < outsz - 1 ? width : outsz - 1;
	for (int i = 0; i < cnt; i++) {
		out[i] = loop[(tui_marquee_pos + i) % looplen];
	}
	out[cnt] = 0;
}

static void tui_render(tui_state_t *s)
{
	tui_width = tui_get_width();
	int inner = tui_width - 2; /* usable columns between the two borders */
	char line[768];
	char bar[384];
	char marquee[400];

	if (tui_drawn) {
		printf("\e[%dA", TUI_LINES); /* move cursor back to top of panel */
	}

	tui_hline("\xe2\x94\x8c", "\xe2\x94\x80", "\xe2\x94\x90", inner); /* ┌──┐ */

	/* line 1: title + playlist position */
	if (s->track_total > 0) {
		snprintf(line, sizeof(line),
		         " " C_NOTE_ICON "\xe2\x99\xaa" C_RESET " " C_TITLE "aplay+" C_RESET
		         "  " C_LABEL "[%d/%d]" C_RESET "%s",
		         s->track_index, s->track_total,
		         s->paused ? "  " C_PAUSE "\xe2\x8f\xb8 PAUSED" C_RESET : "");
	} else {
		snprintf(line, sizeof(line),
		         " " C_NOTE_ICON "\xe2\x99\xaa" C_RESET " " C_TITLE "aplay+" C_RESET "%s",
		         s->paused ? "  " C_PAUSE "\xe2\x8f\xb8 PAUSED" C_RESET : "");
	}
	tui_line(line, inner);

	/* line 2: filename, colorized, scrolling "telop" if it doesn't fit */
	{
		int content_w = inner - 1;
		tui_marquee(s->filename, content_w, marquee, sizeof(marquee));
		snprintf(line, sizeof(line), " " C_FILE "%s" C_RESET, marquee);
	}
	tui_line(line, inner);

	/* line 3: format info */
	if (s->bits > 0) {
		snprintf(line, sizeof(line),
		         " " C_CODEC "%-5s" C_RESET " " C_LABEL "%dHz %dbit %dch" C_RESET
		         "  " C_DEVICE "dev:%s" C_RESET,
		         s->codec ? s->codec : "", s->rate, s->bits, s->channels,
		         s->device ? s->device : "");
	} else {
		snprintf(line, sizeof(line),
		         " " C_CODEC "%-5s" C_RESET " " C_LABEL "%dHz %dch" C_RESET
		         "  " C_DEVICE "dev:%s" C_RESET,
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
			int tm = (int)(s->total) / 60, ts_ = (int)(s->total) % 60;
			if (s->total > 0.0) {
				snprintf(line, sizeof(line),
				         " " C_TIME "%02d:%02d" C_RESET " %s " C_TIME "%02d:%02d" C_RESET,
				         cm, cs, bar, tm, ts_);
			} else {
				snprintf(line, sizeof(line),
				         " " C_TIME "%02d:%02d" C_RESET " %s " C_TIME "--:--" C_RESET,
				         cm, cs, bar);
			}
		} else {
			double ratio = (s->total_raw > 0) ?
			               ((double)s->cur_raw / (double)s->total_raw) : -1.0;
			tui_bar(bar, sizeof(bar), barw, ratio);
			if (s->total_raw > 0) {
				snprintf(line, sizeof(line),
				         " " C_TIME "%llu" C_RESET " %s " C_TIME "%llu %s" C_RESET,
				         (unsigned long long)s->cur_raw, bar,
				         (unsigned long long)s->total_raw,
				         s->unit ? s->unit : "");
			} else {
				snprintf(line, sizeof(line),
				         " " C_TIME "%llu" C_RESET " %s " C_TIME "%s" C_RESET,
				         (unsigned long long)s->cur_raw, bar,
				         s->unit ? s->unit : "");
			}
		}
	}
	tui_line(line, inner);

	/* line 5: volume / crosstalk / loop, plus any transient note */
	{
		int pct = (int)(s->volume * 100.0f + 0.5f);
		const char *vol_color = pct > 66 ? C_VOL_HI : (pct > 33 ? C_VOL_MID : C_VOL_LO);
		const char *xtc_color = s->xtc_on ? C_ON : C_OFF;
		const char *loop_color = s->loop_mode ? C_ON : C_OFF;
		char xtc_part[64];
		if (s->xtc_on) {
			snprintf(xtc_part, sizeof(xtc_part), "%sXTC:ON(%.2f)%s", xtc_color, s->xtc_atten, C_RESET);
		} else {
			snprintf(xtc_part, sizeof(xtc_part), "%sXTC:OFF%s", xtc_color, C_RESET);
		}
		snprintf(line, sizeof(line),
		         " " C_LABEL "Vol:" C_RESET "%s%3d%%" C_RESET "  %s  " C_LABEL "Loop:" C_RESET "%s%s" C_RESET "%s%s%s",
		         vol_color, pct, xtc_part,
		         loop_color, s->loop_mode ? "ON" : "OFF",
		         s->note ? "  " C_NOTEMSG : "", s->note ? s->note : "", s->note ? C_RESET : "");
	}
	tui_line(line, inner);

	/* line 6: keybind footer */
	tui_line(" " C_FOOTER "[Space]Pause [\xe2\x86\x90\xe2\x86\x92]Seek" "\xc2\xb1" "10s [\xe2\x86\x91\xe2\x86\x93]Vol"
	         " [C]rosstalk [B]ack [Q/Esc]Quit" C_RESET, inner);

	tui_hline("\xe2\x94\x94", "\xe2\x94\x80", "\xe2\x94\x98", inner); /* └──┘ */

	fflush(stdout);
	tui_drawn = 1;
}

#endif
