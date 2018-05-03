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
	munmap(file_data, len);
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

/*int play_wma(char *name)
{
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

	CodecContext cc;
	wma_decode_init_fixed(&cc);
	frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, &info);
	if (!frame_size) {
		printf("Error: not a valid MP3 audio file!\n");
		return 1;
	}

	printf("%dHz %dch\n", info.sample_rate, info.channels);
	AUDIO a;
	if (AUDIO_init(&a, dev, info.sample_rate, info.channels, FRAMES, 1)) {
		return 1;
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0)) {
		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
		AUDIO_wait(&a, 100);
		if (key(&a)) break;

		printf("\r%d", c);
		c += frame_size;

		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	return 0;
}*/

#define AAC_BUF_SIZE		(AAC_MAX_NCHANS * AAC_MAX_NSAMPS)	// AAC output buffer
#define DECODE_NUM_STATES	2
// read big endian 16-Bit from fileposition
uint16_t fread16(size_t pos, int fd/*FILE *fp*/)
{
	uint16_t tmp16;
//	fseek(fp, pos, SEEK_SET);
//	fread((uint8_t*)&tmp16, sizeof(uint16_t), 1, fd);
	lseek(fd, pos, SEEK_SET);
	read(fd, &tmp16, sizeof(uint16_t));
	return REV16(tmp16);
}
// read big endian 32-Bit from fileposition
uint32_t fread32(size_t pos, int fd/*FILE *fp*/)
{
	uint32_t tmp32;
//	fseek(fp, pos, SEEK_SET);
//	fread((uint8_t*)&tmp32, sizeof(uint32_t), 1, fd);
	lseek(fd, pos, SEEK_SET);
	read(fd, &tmp32, sizeof(uint32_t));
	return REV32(tmp32);
}
typedef struct { unsigned int position; unsigned int size; } _ATOM;
typedef struct { uint32_t size; char name[4]; } _ATOMINFO;
_ATOM findMp4Atom(const char *atom, const uint32_t posi, const int loop, int fd/*FILE *fp*/)
{
	int r;
	_ATOM ret;
	_ATOMINFO atomInfo;

	ret.position = posi;
	do {
		r = lseek(fd, ret.position, SEEK_SET);
		read(fd, &atomInfo, sizeof(atomInfo));
		//r = fseek(fp, ret.position, SEEK_SET);
		//fread((uint8_t*)&atomInfo, sizeof(atomInfo), 1, fd);
		ret.size = REV32(atomInfo.size);
		if (!strncmp(atom, atomInfo.name, 4)) return ret;
		ret.position += ret.size;
	} while (loop && /*!r*/r>=0);

	printf("[%s] is not found!\n", atom);
	ret.position = 0;
	ret.size = 0;
	return ret;
}
int setupMp4(HAACDecoder aac, AACFrameInfo *aacFrameInfo, int fd/*FILE *fp*/)
{
	_ATOM ftyp = findMp4Atom("ftyp", 0, 0, fd);
	if (!ftyp.size) return 0; // no mp4/m4a file

	// go through the boxes to find the interesting atoms:
	uint32_t moov = findMp4Atom("moov", 0, 1, fd).position;
	uint32_t trak = findMp4Atom("trak", moov + 8, 1, fd).position;
	uint32_t mdia = findMp4Atom("mdia", trak + 8, 1, fd).position;

	// determine duration:
	uint32_t mdhd = findMp4Atom("mdhd", mdia + 8, 1, fd).position;
	uint32_t timescale = fread32(mdhd + 8 + 0x0c, fd);
	unsigned int duration = 1000.0 * ((float)fread32(mdhd + 8 + 0x10, fd) / (float)timescale);

	// MP4-data has no aac-frames, so we have to set the parameters by hand.
	uint32_t minf = findMp4Atom("minf", mdia + 8, 1, fd).position;
	uint32_t stbl = findMp4Atom("stbl", minf + 8, 1, fd).position;
	// stsd sample description box: - infos to parametrize the decoder
	_ATOM stsd = findMp4Atom("stsd", stbl + 8, 1, fd);
	if (!stsd.size) return 0; // something is not ok

	uint16_t channels = fread16(stsd.position + 8 + 0x20, fd);
	//uint16_t channels = 1;
	//uint16_t bits = fread16(stsd.position + 8 + 0x22); //not used
	uint16_t samplerate = fread32(stsd.position + 8 + 0x26, fd);

//	setupDecoder(channels, samplerate, AAC_PROFILE_LC);
	memset(aacFrameInfo, 0, sizeof(AACFrameInfo));
	aacFrameInfo->nChans = channels;
	//aacFrameInfo.bitsPerSample = bits; not used
	aacFrameInfo->sampRateCore = samplerate;
	aacFrameInfo->profile = AAC_PROFILE_LC;
	AACSetRawBlockParams(aac, 0, aacFrameInfo);

	// stco - chunk offset atom:
	uint32_t stco = findMp4Atom("stco", stbl + 8, 1, fd).position;

	// number of chunks:
	uint32_t nChunks = fread32(stco + 8 + 0x04, fd);
	// first entry from chunk table:
	uint32_t firstChunk = fread32(stco + 8 + 0x08, fd);
	// last entry from chunk table:
	uint32_t lastChunk = fread32(stco + 8 + 0x04 + nChunks * 4, fd);

	if (nChunks == 1) {
		_ATOM mdat =  findMp4Atom("mdat", 0, 1, fd);
		lastChunk = mdat.size;
	}

//	lseek(fd, firstChunk, SEEK_SET);
	//fseek(fp, firstChunk, SEEK_SET);
#if 0
	Serial.print("mdhd duration=");
	Serial.print(duration);
	Serial.print(" ms, stsd: chan=");
	Serial.print(channels);
	Serial.print(" samplerate=");
	Serial.print(samplerate);
	Serial.print(" nChunks=");
	Serial.print(nChunks);
	Serial.print(" firstChunk=");
	Serial.println(firstChunk, HEX);
	Serial.print(" lastChunk=");
	Serial.println(lastChunk, HEX);
#endif
	return firstChunk;
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

	int len = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
//	stream_pos = file_data;
//	bytes_left = len;// - 100;

	/*FILE *fp = fopen(name, "rb");
	if (!fp) {
		printf("Error: cannot open `%s`\n", name);
		return 1;
	}*/

//	short *left  = (short*)malloc(AAC_BUF_SIZE * sizeof(int16_t));
//	short *right = (short*)malloc(AAC_BUF_SIZE * sizeof(int16_t));
	AACFrameInfo info;
	HAACDecoder aac = AACInitDecoder();

	int chunk = setupMp4(aac, &info, fd);
	if (!chunk) {
/*		// NO MP4. Do we have an ID3TAG ?
		fseek(0);
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

	printf("%dHz %dch\n", info.sampRateCore, info.nChans);
	AUDIO a;
	if (AUDIO_init(&a, dev, info.sampRateCore, info.nChans, FRAMES, 1)) {
		return 1;
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && !key(&a)) {
//		int nextSync = AACFindSyncWord(stream_pos, bytes_left);
//		stream_pos += nextSync;
		int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
//		int r = AACDecode(aac, &stream_pos, &c, sample_buf);
//		stream_pos += nextSync;
//		bytes_left -= nextSync;
//		bytes_left = len - (stream_pos-file_data);
//		printf("\r%d %d", bytes_left, c++);
		printf("\r%d %d", stream_pos, c++);
//		printf("\r%d", c);
		if (!r) {
			AACGetLastFrameInfo(aac, &info);
			AUDIO_play(&a, (char*)sample_buf, info.outputSamps/*/2*//info.nChans);
			AUDIO_wait(&a, 100);
		} else {
			printf("\nAAC decode error %d\n", r);
			int nextSync = AACFindSyncWord(stream_pos, bytes_left);
			stream_pos += nextSync;
		}
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	//fclose(fp);
	munmap(file_data, len);
	close(fd);
	return 0;
}

void play_dir(char *name, int flag)
{
	char path[1024], ext[10];
	int num;

	LS_LIST *ls = ls_dir(name, flag, &num);
	for (int i=0; i<num; i++) {
		printf("%s\n", ls[i].d_name);
		snprintf(path, 1024, "%s", ls[i].d_name);

		char *e = findExt(ls[i].d_name);
		//printf("ext:%s[%s]\n", e, strstr(e, "flac"));
		if (strstr(e, "flac")) play_flac(path);
		else if (strstr(e, "mp3")) play_mp3(path);
		else if (strstr(e, "mp4")) play_aac(path);
		else if (strstr(e, "m4a")) play_aac(path);
		else if (strstr(e, "ogg")) play_ogg(path);
		else if (strstr(e, "wav")) play_wav(path);
		//else if (strstr(e, "wma")) play_wma(path);

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

