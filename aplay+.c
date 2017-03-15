// ©2017 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>

#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <dirent.h>
int selects(const struct dirent *dir)
{
	if(dir->d_name[0] == '.') {
		return 0;
	}
	return 1;
}

//char *dev = "default";	// "plughw:0,0"
char *dev = "hw:0,0";	// BitPerfect

void play_wav(char *name)
{
	drwav wav;
	if (drwav_init_file(&wav, name)) {
		printf("%dHz %dch\n", wav.sampleRate, wav.channels);

		AUDIO a;
		AUDIO_init(&a, dev, wav.sampleRate, wav.channels, 2048, 1);

		while (drwav_read_s16(&wav, a.frames * wav.channels, (dr_int16*)a.buffer) > 0) {
			AUDIO_play(&a);
			AUDIO_wait(&a, 100);
		}

		AUDIO_close(&a);
		drwav_uninit(&wav);
	}
}

void play_flac(char *name)
{
	drflac *flac = drflac_open_file(name);
	printf("%dHz %dch\n", flac->sampleRate, flac->channels);

	AUDIO a;
	AUDIO_init(&a, dev, flac->sampleRate, flac->channels, /*16384*/2048, 1);

	if (flac) {
		while (drflac_read_s16(flac, a.frames * flac->channels, (dr_int16*)a.buffer) > 0) {
			AUDIO_play(&a);
			AUDIO_wait(&a, 100);
		}
	}

	AUDIO_close(&a);
}

void play_dir(char *name)
{
	char path[1024];
	struct dirent **namelist;

	int r = scandir(name, &namelist, selects, alphasort);
	if (r == -1) return;

	for (int i=0; i<r; ++i) {
		printf("%s\n", namelist[i]->d_name);
		snprintf(path, 1024, "%s/%s", name, namelist[i]->d_name);

		if (strstr(namelist[i]->d_name, ".flac")) play_flac(path);
		else if (strstr(namelist[i]->d_name, ".wav")) play_wav(path);

		free(namelist[i]);
	}

	free(namelist);
}

int main(int argc, char *argv[])
{
	if (argc>2) dev = argv[2];
	play_dir(argv[1]);
}

