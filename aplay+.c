// ©2017-2018 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>
#include <sys/mman.h>

#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "minimp3.h"
//#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR
#define AAC_ENABLE_SBR
#include "uaac.h"
#include "uwma.h"
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

	int len = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	stream_pos = (unsigned char *) file_data;
	bytes_left = len - 100;

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
	while ((bytes_left >= 0) && (frame_size > 0) && !key(&a)) {
		printf("\r%d", c);

		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
		AUDIO_wait(&a, 100);

		c += frame_size;
		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	munmap(file_data, len);
	mp3_done(mp3);
	close(fd);
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

int play_wma(char *name)
{
	void *file_data;
	unsigned char *stream_pos;
	short sample_buf[MAX_CODED_SUPERFRAME_SIZE];
	int bytes_left;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		printf("Error: cannot open `%s`\n", name);
		return 1;
	}

	int len = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	stream_pos = (unsigned char *) file_data;
	bytes_left = len;// - 100;

	CodecContext cc;
	wma_decode_init_fixed(&cc);
	printf("%dHz %dch\n", cc.sample_rate, cc.channels);
	AUDIO a;
	if (AUDIO_init(&a, dev, cc.sample_rate, cc.channels, FRAMES, 1)) {
		return 1;
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && !key(&a)) {
		int size;
		int frame_size = wma_decode_superframe(&cc, stream_pos, &size, (uint8_t*)sample_buf, MAX_CODED_SUPERFRAME_SIZE);
		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, size/cc.channels);
		AUDIO_wait(&a, 100);

		printf("\r%d", c);
		c += frame_size;
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	wma_decode_end(&cc);
	munmap(file_data, len);
	close(fd);
	return 0;
}

int play_aac(char *name)
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

#if 0
	int len = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
//	stream_pos = file_data;
//	bytes_left = len;// - 100;

	AACFrameInfo info;
	HAACDecoder aac = AACInitDecoder();

	int chunk = uaac_setupMp4(aac, &info, fd);
	if (!chunk) {
		printf("!!!! %dHz %dch\n", info.sampRateCore, info.nChans);
/*		// NO MP4. Do we have an ID3TAG ?
		lseek(fd, 0, SEEK_SET);
//		fseek(0);
		// Read-ahead 10 Bytes to detect ID3
		sd_left = fread(sd_buf, 10);
		// Skip ID3, if existent
		uint32_t skip = skipID3(sd_buf);
		if (skip) {
			size_id3 = skip;
			int b = skip & 0xfffffe00;
			fseek(b);
			sd_left = 0;
			//Serial.print("ID3");
		} else size_id3 = 0;*/
	}
	stream_pos = file_data + chunk;
	bytes_left = len - chunk;
	printf("%x %x %x %x\n", stream_pos[0], stream_pos[1], stream_pos[2], stream_pos[3]);
#else
	int samplerate, channels;
	file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels);
	stream_pos = file_data;

	AACFrameInfo info;
	memset(&info, 0, sizeof(AACFrameInfo));
	info.nChans = channels;
	//info.bitsPerSample = bits; // not used
	info.sampRateCore = samplerate;
	info.profile = AAC_PROFILE_LC;
	HAACDecoder aac = AACInitDecoder();
	AACSetRawBlockParams(aac, 0, &info);
#endif

	printf("%dHz %dch\n", info.sampRateCore, info.nChans);
	AUDIO a;
	if (AUDIO_init(&a, dev, info.sampRateCore, info.nChans, FRAMES, 1)) {
		return 1;
	}

	printf("\e[?25l");
	while ((bytes_left >= 0) && !key(&a)) {
		int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
		printf("\r%d %d", (int)(stream_pos-file_data), bytes_left);
		if (!r) {
			AACGetLastFrameInfo(aac, &info);
			AUDIO_play(&a, (char*)sample_buf, info.outputSamps/info.nChans);
			AUDIO_wait(&a, 100);
		} else {
			int nextSync = AACFindSyncWord(stream_pos, bytes_left);
			if (nextSync==-1) break;
			printf("\nAAC decode error %d\n", r);
			break;
//			printf("next %d\n", nextSync);
//			stream_pos += nextSync;
//			bytes_left -= nextSync;
		}
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	AACFreeDecoder(aac);
//	munmap(file_data, len);
	close(fd);
	return 0;
}

void play_dir(char *name, char *type, int flag)
{
	char path[1024], ext[10];
	int num;

	LS_LIST *ls = ls_dir(name, flag, &num);
	for (int i=0; i<num; i++) {
		char *e = findExt(ls[i].d_name);
		//printf("ext:%s[%s]\n", e, strstr(e, "flac"));
		if (type) {
			if (!strstr(e, type)) continue;
		}

		printf("\n%s\n", ls[i].d_name);
		snprintf(path, 1024, "%s", ls[i].d_name);

		if (strstr(e, "flac")) play_flac(path);
		else if (strstr(e, "mp3")) play_mp3(path);
		else if (strstr(e, "mp4")) play_aac(path);
		else if (strstr(e, "m4a")) play_aac(path);
		else if (strstr(e, "ogg")) play_ogg(path);
		else if (strstr(e, "wav")) play_wav(path);
//		else if (strstr(e, "wma")) play_wma(path);

		if (cmd=='\\') i -= 2;
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
		"-t                 File type [flac mp3 wma...]\n"
		"\n",
		argv[0]);
}

int main(int argc, char *argv[])
{
	int flag = 0;
	char *dir = ".";
	char *type = 0;
	struct parg_state ps;
	int c;

	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hd:rxt:")) != -1) {
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
		case 't':
			type = (char*)ps.optarg;
			break;
		case 'h':
		//default:
			usage(stderr, argc, argv);
			return 1;
		}
	}

	init_keyboard();
	play_dir(dir, type, flag);
	close_keyboard();

	return 0;
}

