// ©2017-2026 Yuichiro Nakada
// MIT license
//
// clang -Os -o aplay+ aplay+.c -lasound                          (dr_flac, default)
// clang -Os -DUSE_FOXEN_FLAC -o aplay+ aplay+.c -lasound         (foxen-flac)
// Generate flac.h (required for foxen-flac): ./make_flac_h.sh

#define APLAY_ENGINE_IMPLEMENTATION
#include "aplay+engine.h"

static void wait_for_keypress(void)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	select(STDIN_FILENO + 1, &fds, NULL, NULL, NULL);
}

int key(AUDIO *a, tui_state_t *ts)
{
	static struct timespec last_check = {0, 0};
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long elapsed_us = (now.tv_sec - last_check.tv_sec) * 1000000L +
	                  (now.tv_nsec - last_check.tv_nsec) / 1000L;
	if (elapsed_us < KEY_POLL_INTERVAL_US) {
		return 0;
	}
	last_check = now;

	if (!kbhit()) {
		return 0;
	}

	int c = cmd = getchar();
	if (verbose) {
		printf("%x\n", c);
	}

	if (c == 0x1b) {
		int tries = 0;
		while (!kbhit() && tries < 20) { usleep(500); tries++; }
		if (kbhit()) {
			int c2 = getchar();
			if (c2 == '[' || c2 == 'O') {
				tries = 0;
				while (!kbhit() && tries < 20) { usleep(500); tries++; }
				if (kbhit()) {
					int c3 = getchar();
					switch (c3) {
					case 'A': c = KEY_UP;    break;
					case 'B': c = KEY_DOWN;  break;
					case 'C': c = KEY_RIGHT; break;
					case 'D': c = KEY_LEFT;  break;
					default:  c = 0;         break;
					}
				} else {
					c = 0;
				}
			}
		}
		cmd = c;
	}

	if (aplay_handle_device_keys(a, ts, c)) {
		cmd = 0;
		return 0;
	}

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

	if (c == 0x20) {
		snd_pcm_pause(a->handle, 1);
		if (ts) { ts->paused = 1; tui_render(ts); }
		wait_for_keypress();
		getchar();
		cmd = 0;
		snd_pcm_pause(a->handle, 0);
		snd_pcm_prepare(a->handle);
		if (ts) { ts->paused = 0; tui_render(ts); }
		return 0;
	}

	if (c == 0x09) {
		AUDIO_release(a);
		if (ts) { ts->paused = 1; tui_render(ts); }
		wait_for_keypress();
		getchar();
		cmd = 0;
		AUDIO_reopen(a);
		apply_alsa_volume();
		if (ts) { ts->paused = 0; tui_render(ts); }
		return 0;
	}

	return c;
}

void usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
	        "Usage: %s [options] dir\n\n"
	        "Options:\n"
	        "-h                 Print this help message\n"
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
	        "-D                 Speaker distance for crosstalk cancellation\n"
	        "-T                 Enable test mode (sine wave: left, right, pan)\n"
	        "-e                 Start playback with real-time PCM->DSD64 (DoP)\n"
	        "                   output. Can be toggled live during playback with\n"
	        "                   the 'e' key (WAV/FLAC/MP3/OGG/WMA/AAC)\n"
	        "-o <path>          Also write the raw DSD bitstream generated whenever\n"
	        "                   DSD(DoP) output is active to <path> (optional; off\n"
	        "                   by default, playback is unaffected either way)\n"
	        "\n"
	        "During playback:\n"
	        "  Space              Pause / resume\n"
	        "  Tab                Pause / resume, releasing the ALSA device meanwhile\n"
	        "                     (so other apps can use the sound card)\n"
	        "  Left / Right       Seek -/+ %ds (FLAC, MP3, WAV, OGG)\n"
	        "  Up / Down          Volume +/- %.0f%%\n"
	        "  C                  Toggle crosstalk cancellation\n"
	        "  + / -              Adjust crosstalk attenuation\n"
	        "  E                  Toggle real-time PCM<->DSD64(DoP) output\n"
	        "  S                  Toggle wave super resolution\n"
	        "  F                  Cycle playlist format filter (ALL/flac/mp3/m4a/ogg/wav/wma/dsf/dff)\n"
	        "  D                  Cycle ALSA output device (live)\n"
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
	int device_specified = 0;

	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hd:frxs:t:pclvDTVeo:")) != -1) {
		switch (c) {
		case 1:  dir = (char*)ps.optarg; break;
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

	if (flag & USE_TEST_MODE) {
		int format = flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;
		play_test_mode(format, flag);
	} else {
		play_dir(dir, type, regexp, flag);
	}

	if (g_dsd_raw_file) {
		fclose(g_dsd_raw_file);
	}

	if (clock) {
		set_cpu("ondemand");
	}

	return 0;
}
