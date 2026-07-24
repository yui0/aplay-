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
#define C_DIR		"\e[38;5;109m"		/* muted teal-blue - current directory */

typedef struct {
	int track_index;	/* 1-based current track number (0 = unknown) */
	int track_total;	/* total tracks in playlist (0 = unknown) */
	const char *filename;	/* path/name of the current file */
	const char *dir;	/* directory the current file lives in, shown right-aligned
				 * on the title line (NULL/empty = don't show) */
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
	const char *format_filter; /* active playlist format filter (e.g. "flac"), NULL/"" = ALL */

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

/* ---- UTF-8 helpers -------------------------------------------------
 * The panel needs to know, in bytes, how long a UTF-8 sequence is (so it
 * never splits one in half -- that's what causes garbled/mojibake output
 * when truncating or scrolling Japanese text), and in terminal columns,
 * how wide the resulting character is (CJK ideographs, kana, hangul and
 * fullwidth forms are double-width in virtually every terminal). */
static int utf8_seq_len(unsigned char c)
{
	if ((c & 0x80) == 0) {
		return 1;
	}
	if ((c & 0xE0) == 0xC0) {
		return 2;
	}
	if ((c & 0xF0) == 0xE0) {
		return 3;
	}
	if ((c & 0xF8) == 0xF0) {
		return 4;
	}
	return 1; /* stray continuation/invalid byte: treat as 1 to keep progressing */
}

/* Validate that s[1..seqlen-1] are proper UTF-8 continuation bytes; if not
 * (truncated/corrupt input), fall back to treating just the lead byte as
 * a single "character" instead of reading past the end of the string. */
static int utf8_valid_seq(const char *s, int seqlen)
{
	for (int i = 1; i < seqlen; i++) {
		if (!s[i] || ((unsigned char)s[i] & 0xC0) != 0x80) {
			return 0;
		}
	}
	return 1;
}

static uint32_t utf8_decode(const char *s, int seqlen)
{
	unsigned char c0 = (unsigned char)s[0];
	uint32_t cp;
	switch (seqlen) {
	case 2: cp = c0 & 0x1F; break;
	case 3: cp = c0 & 0x0F; break;
	case 4: cp = c0 & 0x07; break;
	default: return c0;
	}
	for (int i = 1; i < seqlen; i++) {
		cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
	}
	return cp;
}

/* 1 for normal-width codepoints, 2 for East Asian Wide/Fullwidth ranges
 * (covers hiragana, katakana, CJK ideographs, hangul, fullwidth forms). */
static int utf8_char_width(uint32_t cp)
{
	if ((cp >= 0x1100 && cp <= 0x115F) ||
	    cp == 0x2329 || cp == 0x232A ||
	    (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
	    (cp >= 0xAC00 && cp <= 0xD7A3) ||
	    (cp >= 0xF900 && cp <= 0xFAFF) ||
	    (cp >= 0xFE30 && cp <= 0xFE6F) ||
	    (cp >= 0xFF00 && cp <= 0xFF60) ||
	    (cp >= 0xFFE0 && cp <= 0xFFE6) ||
	    (cp >= 0x20000 && cp <= 0x3FFFD)) {
		return 2;
	}
	return 1;
}

/* Decode the UTF-8 char at *s (advancing past any color escapes is the
 * caller's job); returns byte length and, via *out_w, its column width. */
static int utf8_next(const char *s, int *out_w)
{
	int seqlen = utf8_seq_len((unsigned char)*s);
	if (seqlen > 1 && !utf8_valid_seq(s, seqlen)) {
		seqlen = 1;
	}
	*out_w = (seqlen == 1) ? 1 : utf8_char_width(utf8_decode(s, seqlen));
	return seqlen;
}

typedef struct {
	const char *ptr;
	int bytelen;
	int width;
} utf8_cell_t;

/* Split `s` into an array of UTF-8 "cells" (one per character) with their
 * byte length and display width, up to max_cells entries. Used by the
 * marquee so it can rotate/truncate on character boundaries. */
static int utf8_decompose(const char *s, utf8_cell_t *cells, int max_cells)
{
	int n = 0;
	while (*s && n < max_cells) {
		int w;
		int seqlen = utf8_next(s, &w);
		cells[n].ptr = s;
		cells[n].bytelen = seqlen;
		cells[n].width = w;
		n++;
		s += seqlen;
	}
	return n;
}

/* Visible-column length of `s`, skipping over any "\e[...m" SGR escape
 * sequences so color codes never throw off panel alignment, and counting
 * multi-byte UTF-8 characters (e.g. Japanese) at their correct terminal
 * column width instead of one column per byte. */
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
			int w;
			s += utf8_next(s, &w);
			len += w;
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
				int cw;
				int seqlen = utf8_next(p, &cw);
				if (printed + cw > w) {
					break; /* would split a wide (e.g. Japanese) char across the border */
				}
				fwrite(p, 1, (size_t)seqlen, stdout);
				p += seqlen;
				printed += cw;
			}
		}
		for (int i = printed; i < w; i++) {
			fputc(' ', stdout); /* pad leftover column if a wide char didn't fit */
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

/* Scrolling "telop" for text that doesn't fit the panel width. Advances
 * by wall-clock time (TUI_MARQUEE_CPS chars/sec) so speed is independent
 * of text length and of how often tui_render() is called. Each on-screen
 * telop (filename, directory, ...) needs its own state so they don't reset
 * or jump when one changes but not the other. */
typedef struct {
	char last[512];
	int pos;
	struct timespec t0;
} tui_marquee_state_t;

#define TUI_MAX_MARQUEE_CELLS 300

static void tui_marquee_ex(tui_marquee_state_t *ms, const char *text, int width, char *out, int outsz)
{
	if (!text) {
		text = "(unknown)";
	}
	if (strcmp(text, ms->last) != 0) {
		snprintf(ms->last, sizeof(ms->last), "%.500s", text);
		ms->pos = 0;
		clock_gettime(CLOCK_MONOTONIC, &ms->t0);
	}
	if (width <= 0) {
		out[0] = 0;
		return;
	}
	/* Compare *display columns*, not bytes -- a Japanese filename can be
	 * short in characters (and even shorter in bytes-per-column ratio
	 * terms doesn't apply since it's wide) but still overflow `width`. */
	int fwidth = tui_visible_len(text);
	if (fwidth <= width) {
		snprintf(out, (size_t)outsz, "%s", text);
		return;
	}
	char loop[600];
	snprintf(loop, sizeof(loop), "%s   *   ", text);

	/* Decompose into whole characters so rotation/truncation never lands
	 * inside a multi-byte UTF-8 sequence (that mid-sequence split is what
	 * produced the garbled/mojibake output). */
	static utf8_cell_t cells[TUI_MAX_MARQUEE_CELLS];
	int ncells = utf8_decompose(loop, cells, TUI_MAX_MARQUEE_CELLS);
	if (ncells <= 0) {
		out[0] = 0;
		return;
	}
	int looplen = 0; /* total display width of one loop of the marquee text */
	for (int i = 0; i < ncells; i++) {
		looplen += cells[i].width;
	}
	if (looplen <= 0) {
		out[0] = 0;
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	double elapsed = (double)(now.tv_sec - ms->t0.tv_sec) +
	                 (double)(now.tv_nsec - ms->t0.tv_nsec) * 1e-9;
	if (elapsed < 0.0) {
		elapsed = 0.0;
	}
	/* ms->pos is a column offset (not a byte or char index) into the loop */
	ms->pos = ((int)(elapsed * (double)TUI_MARQUEE_CPS)) % looplen;

	/* Find which cell that column offset falls in. If it lands in the
	 * middle of a wide character, start at the *next* cell rather than
	 * emitting half of it. */
	int acc = 0, start_idx = 0;
	for (start_idx = 0; start_idx < ncells; start_idx++) {
		if (acc >= ms->pos) {
			break;
		}
		acc += cells[start_idx].width;
	}

	int col = 0, p = 0, idx = start_idx % ncells;
	for (int guard = 0; guard < ncells && col < width; guard++) {
		utf8_cell_t *c = &cells[idx];
		if (col + c->width > width) {
			break; /* would overflow by splitting this wide char across the edge */
		}
		if (p + c->bytelen >= outsz) {
			break;
		}
		memcpy(out + p, c->ptr, (size_t)c->bytelen);
		p += c->bytelen;
		col += c->width;
		idx = (idx + 1) % ncells;
	}
	out[p] = 0;
}

static tui_marquee_state_t tui_fn_marquee = {0};
static tui_marquee_state_t tui_dir_marquee = {0};

static void tui_marquee(const char *filename, int width, char *out, int outsz)
{
	tui_marquee_ex(&tui_fn_marquee, filename, width, out, outsz);
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

	/* line 1: title + playlist position, with the current directory
	 * right-aligned at the far end of the same line */
	{
		char left[400];
		if (s->track_total > 0) {
			snprintf(left, sizeof(left),
			         " " C_NOTE_ICON "\xe2\x99\xaa" C_RESET " " C_TITLE "aplay+" C_RESET
			         "  " C_LABEL "[%d/%d]" C_RESET "%s",
			         s->track_index, s->track_total,
			         s->paused ? "  " C_PAUSE "\xe2\x8f\xb8 PAUSED" C_RESET : "");
		} else {
			snprintf(left, sizeof(left),
			         " " C_NOTE_ICON "\xe2\x99\xaa" C_RESET " " C_TITLE "aplay+" C_RESET "%s",
			         s->paused ? "  " C_PAUSE "\xe2\x8f\xb8 PAUSED" C_RESET : "");
		}

		int left_len = tui_visible_len(left);
		/* room left for "  " + dir before running into the right border */
		int avail = inner - left_len - 2;
		if (s->dir && s->dir[0] && avail >= 3) {
			char dirbuf[300];
			tui_marquee_ex(&tui_dir_marquee, s->dir, avail, dirbuf, sizeof(dirbuf));
			int dbuf_len = tui_visible_len(dirbuf);
			int pad = inner - left_len - dbuf_len;
			if (pad < 1) {
				pad = 1;
			}
			snprintf(line, sizeof(line), "%s%*s" C_DIR "%s" C_RESET, left, pad, "", dirbuf);
		} else {
			snprintf(line, sizeof(line), "%s", left);
		}
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
		int has_filter = s->format_filter && s->format_filter[0];
		const char *filter_color = has_filter ? C_ON : C_OFF;
		char xtc_part[64];
		if (s->xtc_on) {
			snprintf(xtc_part, sizeof(xtc_part), "%sXTC:ON(%.2f)%s", xtc_color, s->xtc_atten, C_RESET);
		} else {
			snprintf(xtc_part, sizeof(xtc_part), "%sXTC:OFF%s", xtc_color, C_RESET);
		}
		char filter_part[64];
		snprintf(filter_part, sizeof(filter_part), "%s%s%s", filter_color,
		         has_filter ? s->format_filter : "ALL", C_RESET);
		char note_part[300];
		if (s->note) {
			snprintf(note_part, sizeof(note_part), "  " C_NOTEMSG "%s" C_RESET, s->note);
		} else {
			note_part[0] = 0;
		}
		snprintf(line, sizeof(line),
		         " " C_LABEL "Vol:" C_RESET "%s%3d%%" C_RESET "  %s  " C_LABEL "Loop:" C_RESET "%s%s" C_RESET
		         "  " C_LABEL "Fmt:" C_RESET "%s%s",
		         vol_color, pct, xtc_part,
		         loop_color, s->loop_mode ? "ON" : "OFF",
		         filter_part, note_part);
	}
	tui_line(line, inner);

	/* line 6: keybind footer */
	tui_line(" " C_FOOTER "[Tab]Pause [\xe2\x86\x90\xe2\x86\x92]Seek" "\xc2\xb1" "10s [\xe2\x86\x91\xe2\x86\x93]Vol"
	         " [C]rosstalk [F]ormat [B]ack [Q/Esc]Quit" C_RESET, inner);

	tui_hline("\xe2\x94\x94", "\xe2\x94\x80", "\xe2\x94\x98", inner); /* └──┘ */

	fflush(stdout);
	tui_drawn = 1;
}

#endif
