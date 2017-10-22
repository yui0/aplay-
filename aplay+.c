// ©2017 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>
#include <sys/mman.h>

#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "minimp3.h"
#include "stb_vorbis.h"

#define PARG_IMPLEMENTATION
#include "parg.h"

#include "random.h"
#include "ls.h"
#include "kbhit.h"
int cmd;
int key(AUDIO *a)
{
//	if (!kbhit()) return 0;

	int c = readch();
	if (!c) return 0;
	cmd = c;
	printf("%x\n", c);
	if (c==0x20) {
		snd_pcm_pause(a->handle, 1);
		do {
			usleep(1000);	// us
		//} while (!kbhit());
		} while (!readch());
		snd_pcm_pause(a->handle, 0);
		snd_pcm_prepare(a->handle);
		return 0;
	}

	return c;
}

//char *dev = "default";	// "plughw:0,0"
char *dev = "hw:0,0";	// BitPerfect

#define FRAMES	32
void play_wav(char *name)
{
	drwav wav;
	if (drwav_init_file(&wav, name)) {
		printf("%dHz %dch\n", wav.sampleRate, wav.channels);

		AUDIO a;
		if (AUDIO_init(&a, dev, wav.sampleRate, wav.channels, FRAMES, 1)) return;

		while (drwav_read_s16(&wav, a.frames * wav.channels, (drwav_int16*)a.buffer) > 0) {
			AUDIO_play0(&a);
			AUDIO_wait(&a, 100);
			if (key(&a)) break;
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
	if (AUDIO_init(&a, dev, flac->sampleRate, flac->channels, FRAMES, 1)) return;

	if (flac) {
		int c = 0;
		printf("\e[?25l");
		while (drflac_read_s16(flac, a.frames * flac->channels, (drflac_int16*)a.buffer) > 0) {
			AUDIO_play0(&a);
			AUDIO_wait(&a, 100);
			if (key(&a)) break;

			printf("\r%d/%lu", c, flac->totalSampleCount / flac->channels);
			c += a.frames;
		}
		printf("\e[?25h");
	}

	AUDIO_close(&a);
}

int play_mp3(char *name)
{
	mp3_info_t info;
	void *file_data;
	unsigned char *stream_pos;
	short sample_buf[MP3_MAX_SAMPLES_PER_FRAME];
	int bytes_left;
	int frame_size;
	int value;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		printf("Error: cannot open `%s`\n", name);
		return 1;
	}

	bytes_left = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, bytes_left, PROT_READ, MAP_PRIVATE, fd, 0);
	stream_pos = (unsigned char *) file_data;
	bytes_left -= 100;

	mp3_decoder_t mp3 = mp3_create();
	frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, &info);
	if (!frame_size) {
		printf("Error: not a valid MP3 audio file!\n");
		return 1;
	}

	printf("%dHz %dch\n", info.sample_rate, info.channels);
	AUDIO a;
	if (AUDIO_init(&a, dev, info.sample_rate, info.channels, FRAMES/*MP3_MAX_SAMPLES_PER_FRAME*//*frame_size*/, 1)) {
		return 1;
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0)) {
		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
		//AUDIO_play(&a, (char*)sample_buf, frame_size*2);
		AUDIO_wait(&a, 100);
		if (key(&a)) break;

		printf("\r%d", c);
		c += frame_size;

		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	return 0;
}

void play_ogg(char *name)
{
	int n, num_c, error;
	//short *outputs;
	short outputs[FRAMES*2*100];

	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);
	//if (!v) stb_fatal("Couldn't open {%s}", name);
	printf("%dHz %dch\n", v->sample_rate, v->channels);

	AUDIO a;
	if (AUDIO_init(&a, dev, v->sample_rate, v->channels, FRAMES*2, 1)) {
		stb_vorbis_close(v);
		return;
	}

	//while ((n = stb_vorbis_get_frame_short(v, 1, &outputs, FRAMES))) {
	while ((n = stb_vorbis_get_frame_short_interleaved(v, v->channels, outputs, FRAMES*100))) {
		AUDIO_play(&a, (char*)outputs, n);
		AUDIO_wait(&a, 100);
		if (key(&a)) break;
	}

	AUDIO_close(&a);
	stb_vorbis_close(v);
}

/*char *str2lower(char *s)
{
	char *p;
	for (p=s; *p; p++) *p = tolower(*p);
	return s;
}*/
#include <ctype.h>
char *findExt(char *path, char *ext)
{
	//char ext[10];
	char *e = &ext[9];
	*e-- = 0;
	int len = strlen(path)-1;
	for (int i=len; i>len-9; i--) {
		if (path[i] == '.' ) break;
		*e-- = tolower(path[i]);
	}
	return e+1;
}
void play_dir(char *name, int flag)
{
	char path[1024], ext[10];
	int num;

	LS_LIST *ls = ls_dir(name, flag, &num);
	for (int i=0; i<num; i++) {
		printf("%s\n", ls[i].d_name);
		//snprintf(path, 1024, "%s/%s", name, ls[i].d_name);
		snprintf(path, 1024, "%s", ls[i].d_name);

		/*int len = strlen(ls[i].d_name) -5;
		if (strstr(ls[i].d_name+len, ".flac")) play_flac(path);
		else if (strstr(ls[i].d_name+len, ".mp3")) play_mp3(path);
		else if (strstr(ls[i].d_name+len, ".ogg")) play_ogg(path);
		else if (strstr(ls[i].d_name+len, ".wav")) play_wav(path);*/

		char *e = findExt(ls[i].d_name, ext);
		//printf("ext:%s[%s]\n", e, strstr(e, "flac"));
		if (strstr(e, "flac")) play_flac(path);
		else if (strstr(e, "mp3")) play_mp3(path);
		else if (strstr(e, "ogg")) play_ogg(path);
		else if (strstr(e, "wav")) play_wav(path);

		if (cmd=='q' || cmd==0x1b) break;
	}
}

void usage(FILE* fp, int argc, char** argv)
{
	fprintf(fp,
		"Usage: %s [options] dir\n\n"
		"Options:\n"
		"-d <device name>   ALSA device name [default hw:0,0 plughw:0,0...]\n"
		"-h                 Print this message\n"
		"-r                 Recursively search for directory\n"
		"-x                 Random play\n"
		"\n",
		argv[0]);
}

int main(int argc, char *argv[])
{
	int flag = 0;
	char *dir = ".";
	struct parg_state ps;
	int c;

	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hd:rx")) != -1) {
		switch (c) {
		case 1:
			dir = (char*)ps.optarg;
			break;
		case 'd':
			dev = (char*)ps.optarg;
			break;
		case 'r':
			flag |= LS_RECURSIVE;
			break;
		case 'x':
			flag |= LS_RANDOM;
			break;
		case 'h':
		//default:
			usage(stderr, argc, argv);
			return 1;
		}
	}

	init_keyboard();
	play_dir(dir, flag);
	close_keyboard();

	return 0;
}

