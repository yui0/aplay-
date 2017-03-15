// ©2017 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>

#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define TINYFILES_IMPL
#include "tinyfiles.h"

//char *dev = "default";	// "plughw:0,0"
char *dev = "hw:0,0";	// BitPerfect

void play_flac(char *name)
{
	//printf("-> %s\n", name);
	drflac *flac = drflac_open_file(name);
	printf("%s: %dHz %dch\n", name, flac->sampleRate, flac->channels);

	AUDIO a;
	AUDIO_init(&a, dev, flac->sampleRate, flac->channels, /*16384*/2048, 1);

	if (flac) {
		//dr_int16 data[32];
		while (drflac_read_s16(flac, a.frames * flac->channels, (dr_int16*)a.buffer) > 0) {
			/*do {
				snd_pcm_sframes_t avail = snd_pcm_avail_update(a.handle);
				if (avail >= a.size) break;
			} while (1);*/
			/*frame =*/ AUDIO_play(&a);
			AUDIO_wait(&a, 100);
		}
	}

	AUDIO_close(&a);
}

void play_dir(char *name)
{
	tfDIR dir;
	tfDirOpen(&dir, name);

	while (dir.has_next) {
		tfFILE file;
		tfReadFile(&dir, &file);
		printf("%s\n", file.name);
		if (strstr(file.ext, "flac")) play_flac(file.path);
		tfDirNext(&dir);
	}

	tfDirClose(&dir);
}

int main(int argc, char *argv[])
{
	if (argc>2) dev = argv[2];
	play_dir(argv[1]);
}

