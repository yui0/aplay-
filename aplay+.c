// ©2017-2026 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound                          (dr_flac, default)
// clang -Os -DUSE_FOXEN_FLAC -o aplay+ aplay+.c -lasound         (foxen-flac)
// Generate flac.h (required for foxen-flac): ./make_flac_h.sh

#include <stdio.h>
#include <sys/mman.h>
#include <string.h> // for memcpy
#include <math.h>   // for sin
#include <fcntl.h>  // for open, lseek
#include <unistd.h> // for close
#include <limits.h> // for PATH_MAX

#include "alsa.h"
#ifdef USE_FOXEN_FLAC
#define FLAC_IMPLEMENTATION
#include "flac.h"
#else
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#endif
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#ifdef DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#else
#include "minimp3.h"
#endif
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR
//#define AAC_ENABLE_SBR
#include "uaac.h"
#include "uwma.h"
#include "stb_vorbis.h"
#define DSD_DECODER_IMPLEMENTATION
#include "dsd.h"
#define DSD_ENCODER_IMPLEMENTATION
#include "dsd_encoder.h"

#define PARG_IMPLEMENTATION
#include "parg.h"
#include "regexp.h"

#include "random.h"
#include "ls.h"
#include "kbhit.h"
#include "tui.h"

int verbose = 0;
float volume = 1.0f;
int loop_mode = 0;
float speaker_distance_m = 0.5;
int cmd;
int track_index = 0, track_total = 0; // current position in the playlist, for the TUI panel

// -o <path>: when set, every DSD-encoded chunk (whichever track/route produced
// it) is also appended here as a raw bitstream, in addition to (or instead of,
// if playback fails to open) DoP playback. Optional feature, off by default.
char *dsd_file_path = NULL;
FILE *g_dsd_raw_file = NULL;

// Interactive format filter, cycled with the 'f' key during playback (see
// key() below). NULL means "ALL" (no filtering). play_dir() re-checks this
// on every file in its listing, so a change takes effect from the very next
// track without needing to restart the player.
static const char *fmt_cycle[] = { NULL, "flac", "mp3", "m4a", "ogg", "wav", "wma", "dsf", "dff" };
static const int fmt_cycle_n = sizeof(fmt_cycle) / sizeof(fmt_cycle[0]);
static int fmt_filter_idx = 0;
char *fmt_filter = NULL;

//char *dev = "default";  // "plughw:0,0"
char *dev = "hw:0,0";  // BitPerfect

// Synthetic key codes for cursor keys, well outside the char range so they
// can never collide with a real byte read from the terminal.
#define KEY_UP     1001
#define KEY_DOWN   1002
#define KEY_LEFT   1003
#define KEY_RIGHT  1004

#define SEEK_SECONDS   10      // amount of fast-forward/rewind per Left/Right press
#define VOLUME_STEP    0.05f   // amount of volume change per Up/Down press

// Returns a pointer to the filename portion of `path`, stripping any
// leading directory components (everything up to and including the last
// '/'). Used so the TUI marquee shows only the file name, not the full path.
static const char *get_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// Extracts the parent directory portion of path into dir (size bytes).
// "music/rock/song.flac" -> "music/rock", "song.flac" -> "."
// (defined further down, forward-declared here so the play_* functions
// above it can use it to feed tui_state_t.dir for the TUI title line)
static void get_dirpart(const char *path, char *dir, size_t size);

// Pushes the current `volume` (0.0-1.0) out to the ALSA hardware mixer so
// decoded PCM samples stay bit-exact (no software gain on the buffer).
void apply_alsa_volume(void)
{
	AUDIO_set_volume(dev, volume);
}

// `ts`, if non-NULL, is re-rendered immediately on pause/resume so the TUI
// panel reflects the paused state instead of freezing silently.
int key(AUDIO *a, tui_state_t *ts)
{
	if (!kbhit()) {
		return 0;
	}

	int c = cmd = getchar();
	if (verbose) {
		printf("%x\n", c);
	}

	// Cursor keys arrive as multi-byte ANSI escape sequences: ESC '[' <A|B|C|D>.
	// A lone Esc (meant to quit) never has more bytes following it, while a
	// real terminal emits the whole 3-byte sequence in one burst, so a short
	// bounded poll is enough to tell the two apart.
	if (c == 0x1b) {
		int tries = 0;
		while (!kbhit() && tries < 20) {
			usleep(500);
			tries++;
		}
		if (kbhit()) {
			int c2 = getchar();
			if (c2 == '[' || c2 == 'O') {
				tries = 0;
				while (!kbhit() && tries < 20) {
					usleep(500);
					tries++;
				}
				if (kbhit()) {
					int c3 = getchar();
					switch (c3) {
					case 'A':
						c = KEY_UP;
						break;
					case 'B':
						c = KEY_DOWN;
						break;
					case 'C':
						c = KEY_RIGHT;
						break;
					case 'D':
						c = KEY_LEFT;
						break;
					default:
						c = 0; // unrecognized escape sequence, ignore
						break;
					}
				} else {
					c = 0; // incomplete sequence, drop it
				}
			}
			// else: not an escape sequence we understand; c2 is dropped and
			// the original Esc (c == 0x1b) still falls through as Quit below.
		}
		// else: no more bytes followed -> lone Esc, falls through as Quit.
		cmd = c;
	}

	if (c == 'f') {
		// Cycle to the next format filter (ALL -> flac -> mp3 -> ... -> ALL).
		// Takes effect from the next track play_dir() considers; the current
		// track keeps playing.
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
		if (volume > 1.0f) {
			volume = 1.0f;
		}
		if (volume < 0.0f) {
			volume = 0.0f;
		}
		apply_alsa_volume();
		if (ts) {
			ts->volume = volume;
			tui_render(ts);
		}
		return 0;
	}

	if (c==0x09) { // Tab
		// Quick pause: keeps the device open, resumes instantly, but the
		// sound card stays held by us the whole time.
		snd_pcm_pause(a->handle, 1);
		if (ts) {
			ts->paused = 1;
			tui_render(ts);
		}
		do {
			usleep(1000); // us
		} while (!kbhit());
		getchar(); // clear
		cmd = 0;
		snd_pcm_pause(a->handle, 0);
		snd_pcm_prepare(a->handle);
		if (ts) {
			ts->paused = 0;
			tui_render(ts);
		}
		return 0;
	}

	if (c==0x20) { // Space
		// Release pause: actually gives up the device (snd_pcm_close) so
		// another application can use the sound card while we're paused.
		// Slower to resume than Tab, since the device has to be reopened.
		AUDIO_release(a);
		if (ts) {
			ts->paused = 1;
			tui_render(ts);
		}
		do {
			usleep(1000); // us
		} while (!kbhit());
		getchar(); // clear
		cmd = 0;
		AUDIO_reopen(a);
		apply_alsa_volume(); // mixer setting is lost if the device changed hands
		if (ts) {
			ts->paused = 0;
			tui_render(ts);
		}
		return 0;
	}

	return c;
}

#define USE_FLOAT32   128
#define USE_CROSSTALK 256 // Flag for crosstalk cancellation
#define USE_TEST_MODE 512 // Flag for test mode
#define USE_DSD_ENCODE 1024 // -e: re-encode decoded PCM to DSD (DoP) before playback
#define FRAMES        32
//#define FRAMES        128
#define MAX_DELAY_SAMPLES 16 // Maximum delay samples for crosstalk (e.g., 71µs at 44.1kHz is ~3 samples)

// ============================================================
// PCM -> DSD (DoP) リアルタイム出力シンク
//
// WAV/FLACなどをデコードして得られるPCMを、その場でdsd_encoder.hに
// 通してDSD64相当のビットストリームへ変換し、DoP (DSD over PCM) として
// ALSAへ流す。DoPは1DSDバイト2個+マーカー1バイトを24bit PCMサンプルに
// 詰めるため、出力側のALSAレートは (PCMレート * DSD_ENC_OSR / 16) になる
// (例: 44.1kHz -> 176.4kHz)。DoP対応DACであればそのままDSD再生される。
// ============================================================
#ifndef SND_PCM_FORMAT_S24_LE
#define SND_PCM_FORMAT_S24_LE 6
#endif

#define DSD_ENC_OSR         64                          // DSD64 (44.1kHz系なら2.8224MHz)
#define DSD_ENC_INPUT_CHUNK FRAMES                       // 1回にエンコードする入力PCMフレーム数
#define DSD_ENC_DSD_BYTES_PER_CH (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8 + 2)
#define DSD_ENC_DOP_FRAMES  (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 16) // DoP側の1period

typedef struct {
	DSDEncoder *enc;
	AUDIO a;            // DoP出力用に開いたALSAハンドル(monitor_mode時は未使用)
	int channels;
	uint8_t *dsd_buf;    // ch0連続 -> ch1連続 (DSF風レイアウト)
	int32_t *dop_buf;    // DoPパック済み24bit(S24_LE)フレーム

	// 実機のALSAデバイスがDoPの高サンプルレート(pcm_rate*OSR/16)を開けない
	// 場合のフォールバック。dsfファイル再生と同じ考え方で、いま encoder が
	// 作った1bit DSD列を dsd.h の DSDDecoder(実ファイル再生と全く同じ
	// フィルタ/自動ゲイン/リミッタのパイプライン)でその場でPCMへ戻し、
	// 通常のPCM出力として鳴らす(PCM -> DSD -> PCMのソフトウェア往復)。
	int monitor_mode;
	AUDIO mon_a;               // monitor_mode用、ソースPCMレート優先で開くALSAハンドル
	DSDDecoder *mon_decoder;   // dsd_decoder_init_raw()で作る、ファイルを介さないデコーダ
	uint8_t *mon_block_buf;    // dsd_decoder_feed_block()へ渡す、ch毎に隙間なく詰め直したバッファ
	float *mon_pcm_buf;        // 1ブロック分のデコード結果(インターリーブ済みPCM)の一時バッファ
	size_t mon_frames_per_block;

	// 1ブロック(128フレーム前後、176.4kHzなら約0.7ms)ごとにALSAへ書き
	// 出すと、毎回の処理(FIR補完+ΔΣ変調+4段IIRデコード)がリアルタイム
	// 期限にシビアすぎてアンダーラン(音切れ・ビープ音)を起こしやすい。
	// そこでMON_ACCUM_BLOCKS回分をまとめてから1回のALSA書き込みにする。
	float *mon_accum_buf;
	size_t mon_accum_fill;      // 現在たまっているフレーム数
	size_t mon_accum_period;    // ALSAへ書き出す単位(mon_frames_per_block * MON_ACCUM_BLOCKS)
} DsdSink;

// 1回のALSA書き込みにまとめるブロック数。大きくするほどアンダーランに
// 強くなるが、その分レイテンシが増える(再生用途なので大きめでも実害はない)。
#define MON_ACCUM_BLOCKS 8

// dsd_encoder_process_raw()に常にDSD_ENC_INPUT_CHUNKフレームちょうどを渡す
// 運用(dsdaccum_drain()参照)なので、1chあたりの出力バイト数は毎回
// 必ず この値になる(2048bit / 8 = 256、余りは出ない)。
#define DSD_ENC_DSD_BYTES_PER_CH_EXACT (DSD_ENC_INPUT_CHUNK * DSD_ENC_OSR / 8)

// pcm_rate: 変換元PCMのサンプルレート(44100/48000など)。
// 戻り値 0=成功。
int dsdsink_open(DsdSink *sink, const char *dev, int pcm_rate, int channels)
{
	memset(sink, 0, sizeof(*sink));
	sink->channels = channels;

	sink->enc = dsd_encoder_init(channels, pcm_rate, pcm_rate * DSD_ENC_OSR);
	if (!sink->enc) {
		fprintf(stderr, "DSD encoder init failed (channels=%d, rate=%d)\n", channels, pcm_rate);
		return -1;
	}

	sink->dsd_buf = (uint8_t*)malloc(DSD_ENC_DSD_BYTES_PER_CH * channels);
	sink->dop_buf = (int32_t*)malloc((size_t)DSD_ENC_DOP_FRAMES * channels * sizeof(int32_t));
	if (!sink->dsd_buf || !sink->dop_buf) {
		dsd_encoder_free(sink->enc);
		free(sink->dsd_buf);
		free(sink->dop_buf);
		return -1;
	}

	int dop_rate = pcm_rate * DSD_ENC_OSR / 16;
	if (AUDIO_init(&sink->a, (char*)dev, dop_rate, channels, DSD_ENC_DOP_FRAMES, 1, SND_PCM_FORMAT_S24_LE) == 0) {
		if (sink->a.freq == (unsigned int)dop_rate) {
			printf("DSD encode (DoP) output: %d Hz PCM -> DSD64 @ %d Hz DoP / S24_LE / %dch\n",
			       pcm_rate, dop_rate, channels);
			return 0;
		}
		// set_rate_near が別レートへ落とした場合、DoPマーカーは成立しない
		fprintf(stderr, "DoP: rejected ALSA rate remap %d -> %u Hz\n", dop_rate, sink->a.freq);
		AUDIO_close(&sink->a);
		memset(&sink->a, 0, sizeof(sink->a));
	}

	// DoPレート(pcm_rate*OSR/16)がハードウェア側で開けない場合のフォールバック。
	// dsfファイル再生時と全く同じdsd.hのデコードパイプライン(4段フィルタ+
	// 自動ゲイン+常時ソフトリミッタ)を使って、いま作ったばかりのDSDビット列を
	// その場でPCMへ戻し、通常のPCM出力として鳴らす(PCM -> DSD -> PCM往復)。
	fprintf(stderr, "DoP output unavailable at %d Hz, falling back to PCM->DSD->PCM monitor mode (via dsd.h)\n", dop_rate);

	int dsd_rate = pcm_rate * DSD_ENC_OSR;
	sink->mon_decoder = dsd_decoder_init_raw(channels, dsd_rate, DSD_ENC_DSD_BYTES_PER_CH_EXACT);
	if (!sink->mon_decoder) {
		fprintf(stderr, "Failed to init monitor-mode DSD decoder\n");
		dsd_encoder_free(sink->enc);
		free(sink->dsd_buf);
		free(sink->dop_buf);
		return -1;
	}

	// DoP非対応ハードは 176.4kHz も取れないことがほとんど。
	// dsd_choose_pcm_rate() は 176400 を優先するが、AUDIO_init の
	// set_rate_near が別レート(例: 48000)へ黙って落とすと、デコーダは
	// 176.4k 前提のまま書き出すため再生速度が破綻する。
	// → 変換元の pcm_rate を最優先し、DSDレートを割り切れる候補を順に試し、
	//   ALSAが「要求どおりのレート」で開けたものだけ採用する。
	static const int mon_rate_prefs[] = {
		176400, 192000, 88200, 96000, 352800, 384000, 44100, 48000
	};
	int cands[1 + (int)(sizeof(mon_rate_prefs) / sizeof(mon_rate_prefs[0]))];
	int ncands = 0;
	cands[ncands++] = pcm_rate; // ソースレート最優先(通常PCM再生で既に成功している)
	for (size_t i = 0; i < sizeof(mon_rate_prefs) / sizeof(mon_rate_prefs[0]); ++i) {
		int r = mon_rate_prefs[i];
		if (r == pcm_rate) continue;
		if (dsd_rate % r != 0) continue;
		int dec = dsd_rate / r;
		if (dec < 8 || dec > 512) continue;
		cands[ncands++] = r;
	}

	int mon_rate = 0;
	int opened = 0;
	for (int i = 0; i < ncands; ++i) {
		if (dsd_decoder_set_pcm_rate(sink->mon_decoder, cands[i]) != 0) continue;
		sink->mon_frames_per_block = dsd_decoder_frames_per_block(sink->mon_decoder);
		if (sink->mon_frames_per_block == 0) continue;
		// ALSAのperiodは1ブロックではなく MON_ACCUM_BLOCKS 個分まとめた大きさ
		// で開く(1ブロック単位だとFIR+ΔΣ+4段IIRがリアルタイム期限に間に合わず
		// アンダーランしやすい)。
		sink->mon_accum_period = sink->mon_frames_per_block * MON_ACCUM_BLOCKS;
		if (AUDIO_init(&sink->mon_a, (char*)dev, (unsigned int)cands[i], channels,
		               (int)sink->mon_accum_period, 4, SND_PCM_FORMAT_FLOAT_LE) != 0) {
			continue;
		}
		if (sink->mon_a.freq != (unsigned int)cands[i]) {
			// set_rate_near が別レートへ落とした → 速度破綻するので却下
			fprintf(stderr, "monitor: rejected ALSA rate remap %d -> %u Hz\n",
			        cands[i], sink->mon_a.freq);
			AUDIO_close(&sink->mon_a);
			memset(&sink->mon_a, 0, sizeof(sink->mon_a));
			continue;
		}
		mon_rate = cands[i];
		opened = 1;
		break;
	}
	if (!opened) {
		fprintf(stderr, "Failed to open ALSA device for monitor-mode PCM output (tried source %d Hz and fallbacks)\n", pcm_rate);
		dsd_decoder_free(sink->mon_decoder);
		dsd_encoder_free(sink->enc);
		free(sink->dsd_buf);
		free(sink->dop_buf);
		return -1;
	}

	sink->mon_block_buf = (uint8_t*)malloc((size_t)DSD_ENC_DSD_BYTES_PER_CH_EXACT * channels);
	sink->mon_pcm_buf = (float*)malloc(sink->mon_frames_per_block * (size_t)channels * sizeof(float));
	sink->mon_accum_buf = (float*)malloc(sink->mon_accum_period * (size_t)channels * sizeof(float));
	sink->mon_accum_fill = 0;
	if (!sink->mon_block_buf || !sink->mon_pcm_buf || !sink->mon_accum_buf) {
		AUDIO_close(&sink->mon_a);
		dsd_decoder_free(sink->mon_decoder);
		dsd_encoder_free(sink->enc);
		free(sink->dsd_buf);
		free(sink->dop_buf);
		free(sink->mon_block_buf);
		free(sink->mon_pcm_buf);
		free(sink->mon_accum_buf);
		return -1;
	}

	sink->monitor_mode = 1;
	printf("DSD encode (monitor) output: %d Hz PCM -> DSD64 -> %d Hz PCM via dsd.h (DoP hardware unavailable) / %dch\n",
	       pcm_rate, mon_rate, channels);
	return 0;
}

// frames は DSD_ENC_INPUT_CHUNK 以下であること。pcm はインターリーブされた
// float、範囲 -1.0..1.0。内部で補完(アップサンプリング)+ΔΣ変調+DoP
// パッキング(または monitor_mode時はdsd.hによるPCMへのデコード)を行い、
// ALSAへ書き出す。
void dsdsink_write(DsdSink *sink, const float *pcm, size_t frames)
{
	size_t bytes_per_ch = 0;
	if (dsd_encoder_process_raw(sink->enc, pcm, frames, sink->dsd_buf,
	                             DSD_ENC_DSD_BYTES_PER_CH * sink->channels,
	                             &bytes_per_ch) != 0) {
		return;
	}

	const uint8_t *ch_ptrs[DSDENC_MAX_CHANNELS];
	for (int ch = 0; ch < sink->channels; ch++) {
		ch_ptrs[ch] = sink->dsd_buf + (size_t)ch * DSD_ENC_DSD_BYTES_PER_CH;
	}

	// -o が指定されていれば、DoPパック前の生DSDビット列をそのままファイルへ
	// 追記する(ch0連続 -> ch1連続。dsd.hのDSF読み込みと同じレイアウト)。
	if (g_dsd_raw_file) {
		for (int ch = 0; ch < sink->channels; ch++) {
			fwrite(ch_ptrs[ch], 1, bytes_per_ch, g_dsd_raw_file);
		}
	}

	if (sink->monitor_mode) {
		// DoPへは出さず、いま作った1bit DSD列をdsd.hのデコーダ(実ファイル
		// 再生と同じ4段フィルタ+自動ゲイン+常時ソフトリミッタのパイプ
		// ライン)でPCMへ戻し、通常のPCM出力へ流す(PCM -> DSD -> PCM往復)。
		// dsd_decoder_feed_block()が期待する「chごとに隙間なく連続」の
		// レイアウトへ詰め直す(sink->dsd_bufはch間に+2バイトの余裕が
		// あるため、そのままは渡せない)。
		for (int ch = 0; ch < sink->channels; ch++) {
			memcpy(sink->mon_block_buf + (size_t)ch * DSD_ENC_DSD_BYTES_PER_CH_EXACT,
			       ch_ptrs[ch], DSD_ENC_DSD_BYTES_PER_CH_EXACT);
		}
		dsd_decoder_feed_block(sink->mon_decoder, sink->mon_block_buf);
		size_t out_frames = dsd_decoder_read_pcm_frames(sink->mon_decoder,
		                                                 sink->mon_frames_per_block,
		                                                 sink->mon_pcm_buf,
		                                                 SND_PCM_FORMAT_FLOAT_LE);
		// 1ブロック分をすぐALSAへ書き出すとアンダーラン(音切れ・ビープ音)を
		// 起こしやすいため、mon_accum_periodフレームたまるまでアキュムレータへ
		// 貯めてから、まとめて1回で書き出す。
		if (out_frames > 0) {
			memcpy(sink->mon_accum_buf + sink->mon_accum_fill * (size_t)sink->channels,
			       sink->mon_pcm_buf, out_frames * (size_t)sink->channels * sizeof(float));
			sink->mon_accum_fill += out_frames;
		}
		if (sink->mon_accum_fill >= sink->mon_accum_period) {
			memcpy(sink->mon_a.buffer, sink->mon_accum_buf, sink->mon_accum_period * (size_t)sink->channels * sizeof(float));
			AUDIO_play0(&sink->mon_a);
			AUDIO_wait(&sink->mon_a, 100);
			sink->mon_accum_fill = 0;
		}
		return;
	}

	size_t dop_frames = 0;
	// DoPは2バイト(16bit)単位が前提。端数バイトは切り捨てる(次呼び出しの
	// エンコーダ内部状態には影響しない。単に出力の切り捨てなので、通常の
	// FRAMES単位の呼び出しでは端数は出ない)。
	size_t even_bytes = bytes_per_ch & ~((size_t)1);
	dsd_encoder_pack_dop(sink->enc, ch_ptrs, even_bytes, sink->dop_buf, &dop_frames);

	memcpy(sink->a.buffer, sink->dop_buf, dop_frames * sink->channels * sizeof(int32_t));
	AUDIO_play0(&sink->a);
	AUDIO_wait(&sink->a, 100);
}

void dsdsink_close(DsdSink *sink)
{
	if (sink->monitor_mode) {
		// アキュムレータに残っている端数(1周期に満たない最後の数十ms)は
		// 捨てると再生末尾がわずかに欠けるだけなので、無音でパディングして
		// 最後に一度だけ書き出しておく。
		if (sink->mon_accum_fill > 0 && sink->mon_accum_buf) {
			size_t remain = sink->mon_accum_period - sink->mon_accum_fill;
			memset(sink->mon_accum_buf + sink->mon_accum_fill * (size_t)sink->channels,
			       0, remain * (size_t)sink->channels * sizeof(float));
			memcpy(sink->mon_a.buffer, sink->mon_accum_buf, sink->mon_accum_period * (size_t)sink->channels * sizeof(float));
			AUDIO_play0(&sink->mon_a);
			AUDIO_wait(&sink->mon_a, 100);
		}
		AUDIO_close(&sink->mon_a);
		dsd_decoder_free(sink->mon_decoder);
		free(sink->mon_block_buf);
		free(sink->mon_accum_buf);
	} else {
		AUDIO_close(&sink->a);
	}
	dsd_encoder_free(sink->enc);
	free(sink->dsd_buf);
	free(sink->dop_buf);
	free(sink->mon_pcm_buf);
}

// ============================================================
// DsdAccum: デコーダが返すチャンクのサイズは形式によってまちまち
// (WAV/FLAC/MP3(dr_mp3)はFRAMES固定だが、OGG/WMA/AACはもっと大きい
// 単位でまとめて返ってくる)。DSDエンコーダ/DoP出力側はFRAMES単位の
// 固定ペリオドで動かしたいので、ここで一旦floatに詰めてから
// FRAMES単位に区切って流し込む。
// ============================================================
#define DSD_ACCUM_CAPACITY 8192 // 各デコーダの最大チャンクを吸収できる余裕

typedef struct {
	float *buf;
	size_t fill;      // 有効フレーム数
	size_t capacity;  // フレーム単位
	int channels;
} DsdAccum;

void dsdaccum_init(DsdAccum *ac, int channels, size_t capacity_frames)
{
	ac->channels = channels;
	ac->capacity = capacity_frames;
	ac->buf = (float*)malloc(capacity_frames * (size_t)channels * sizeof(float));
	ac->fill = 0;
}
void dsdaccum_free(DsdAccum *ac) { free(ac->buf); ac->buf = NULL; }

void dsdaccum_push_s16(DsdAccum *ac, const int16_t *src, size_t frames)
{
	if (ac->fill + frames > ac->capacity) frames = ac->capacity - ac->fill;
	size_t off = ac->fill * (size_t)ac->channels;
	size_t n = frames * (size_t)ac->channels;
	for (size_t i = 0; i < n; i++) ac->buf[off + i] = src[i] / 32768.0f;
	ac->fill += frames;
}
void dsdaccum_push_f32(DsdAccum *ac, const float *src, size_t frames)
{
	if (ac->fill + frames > ac->capacity) frames = ac->capacity - ac->fill;
	memcpy(ac->buf + ac->fill * (size_t)ac->channels, src, frames * (size_t)ac->channels * sizeof(float));
	ac->fill += frames;
}
// たまっている分をFRAMES単位でDSDエンコーダへ渡し、端数は先頭に詰め直す
void dsdaccum_drain(DsdAccum *ac, DsdSink *sink)
{
	size_t off = 0;
	while (ac->fill - off >= DSD_ENC_INPUT_CHUNK) {
		dsdsink_write(sink, ac->buf + off * (size_t)ac->channels, DSD_ENC_INPUT_CHUNK);
		off += DSD_ENC_INPUT_CHUNK;
	}
	size_t remain = ac->fill - off;
	if (remain > 0 && off > 0) {
		memmove(ac->buf, ac->buf + off * (size_t)ac->channels, remain * (size_t)ac->channels * sizeof(float));
	}
	ac->fill = remain;
}

// ============================================================
// OutRouter: 通常PCM出力とDSD(DoP)出力をTUI上でリアルタイムに
// 切り替えるための出力ルーター。再生中に'e'キーを押すと
// outr_toggle()が呼ばれ、その場でALSAハンドルを閉じ直して
// もう一方の経路へ切り替わる。
// ============================================================
typedef struct {
	int use_dsd;         // 現在の出力モード(0=通常PCM, 1=DSD/DoP経由)
	int channels;
	int sample_rate;
	int format;          // 通常PCM出力時のALSAフォーマット(S16_LE/FLOAT_LE)
	char dev_buf[128];

	AUDIO pcm;
	int pcm_open;

	DsdSink dsd;
	int dsd_open;

	DsdAccum accum;      // DSDモード用のFRAMES単位変換バッファ
	int16_t *i16_scratch; // 通常PCM出力がint16のときのfloat->int16変換先
} OutRouter;

void outr_open_pcm(OutRouter *r)
{
	if (r->pcm_open) return;
	if (r->dsd_open) { dsdsink_close(&r->dsd); r->dsd_open = 0; }
	if (AUDIO_init(&r->pcm, r->dev_buf, r->sample_rate, r->channels, FRAMES, 1, r->format) == 0) {
		r->pcm_open = 1;
	}
}
void outr_open_dsd(OutRouter *r)
{
	if (r->dsd_open) return;
	if (r->pcm_open) { AUDIO_close(&r->pcm); r->pcm_open = 0; }
	// dsdsink_open()はDoPが開けなくてもPCM->DSD->PCMのmonitor_modeへ
	// 自動フォールバックするので、ここに来て失敗するのはエンコーダ初期化や
	// monitor_mode用PCMデバイスすら開けなかった、本当に打つ手がない場合のみ。
	if (dsdsink_open(&r->dsd, r->dev_buf, r->sample_rate, r->channels) == 0) {
		r->dsd_open = 1;
		r->accum.fill = 0; // 切り替え直前の古いデータは破棄
	} else {
		fprintf(stderr, "DSD output unavailable (DoP and monitor mode both failed), falling back to normal PCM\n");
		r->use_dsd = 0;
		outr_open_pcm(r);
	}
}
int outr_init(OutRouter *r, const char *dev_name, int sample_rate, int channels, int format, int start_with_dsd)
{
	memset(r, 0, sizeof(*r));
	r->sample_rate = sample_rate;
	r->channels = channels;
	r->format = format;
	snprintf(r->dev_buf, sizeof(r->dev_buf), "%s", dev_name);
	dsdaccum_init(&r->accum, channels, DSD_ACCUM_CAPACITY);
	r->i16_scratch = (int16_t*)malloc(DSD_ACCUM_CAPACITY * (size_t)channels * sizeof(int16_t));
	r->use_dsd = start_with_dsd ? 1 : 0;
	if (r->use_dsd) outr_open_dsd(r); else outr_open_pcm(r);
	return (r->pcm_open || r->dsd_open) ? 0 : -1;
}
void outr_toggle(OutRouter *r)
{
	r->use_dsd = !r->use_dsd;
	if (r->use_dsd) outr_open_dsd(r); else outr_open_pcm(r);
}
AUDIO *outr_audio(OutRouter *r)
{
	if (!r->use_dsd) return &r->pcm;
	return r->dsd.monitor_mode ? &r->dsd.mon_a : &r->dsd.a;
}
// 現在の出力経路が実際に鳴らしているPCM/DoPサンプルレート。
// 'e'キー切替後にTUIのts.rateへ反映するために使う。
int outr_output_rate(OutRouter *r)
{
	if (!r->use_dsd) return r->sample_rate;
	if (r->dsd.monitor_mode) return (int)r->dsd.mon_a.freq;
	return (int)r->dsd.a.freq;
}
// TUIのts.noteに出す短いステータス文字列。DoPへ実出力できているか、
// DoPが開けずPCM->DSD->PCMのソフトウェア往復(monitor_mode)になっているかを示す。
const char *outr_status_note(OutRouter *r)
{
	if (!r->use_dsd) return "PCM output";
	return r->dsd.monitor_mode ? "DSD monitor (PCM->DSD->PCM, no DoP hw)" : "DSD(DoP)output";
}
void outr_close(OutRouter *r)
{
	if (r->pcm_open) AUDIO_close(&r->pcm);
	if (r->dsd_open) dsdsink_close(&r->dsd);
	dsdaccum_free(&r->accum);
	free(r->i16_scratch);
}

// frames分のfloat PCM(-1.0..1.0)を、現在のモードに応じてDSD(DoP)出力
// またはALSA通常出力へ振り分ける。frames はDSD_ACCUM_CAPACITY以下であること
// (WAV/FLAC/MP3はFRAMES=32、OGG/WMA/AACでも数千程度で十分収まる)。
void outr_feed_float(OutRouter *r, const float *pcm, size_t frames)
{
	if (r->use_dsd) {
		dsdaccum_push_f32(&r->accum, pcm, frames);
		dsdaccum_drain(&r->accum, &r->dsd);
	} else {
		if (r->format == SND_PCM_FORMAT_FLOAT_LE) {
			AUDIO_play(&r->pcm, (char*)pcm, (int)frames);
		} else {
			size_t n = frames * (size_t)r->channels;
			for (size_t i = 0; i < n; i++) {
				float v = pcm[i] * 32767.0f;
				if (v > 32767.0f) v = 32767.0f;
				if (v < -32768.0f) v = -32768.0f;
				r->i16_scratch[i] = (int16_t)v;
			}
			AUDIO_play(&r->pcm, (char*)r->i16_scratch, (int)frames);
		}
		AUDIO_wait(&r->pcm, 100);
	}
}

// Crosstalk cancellation parameters
typedef struct {
	int delay_samples;    // Delay in samples (e.g., 3 for 71µs at 44.1kHz)
	float attenuation;    // Attenuation factor (e.g., 0.9)
	float *delay_buffer;  // Buffer to store delayed samples
	int delay_buffer_size; // Size of delay buffer
	int delay_index;      // Current index in delay buffer
} CrosstalkCancel;

/*void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
    if (channels != 2) return; // Only support stereo
    xtc->delay_samples = (int)(sample_rate * 0.000071); // 71µs delay
    xtc->attenuation = 0.4f; // Natural effect
    xtc->delay_buffer_size = xtc->delay_samples * 2; // Stereo
    if (xtc->delay_buffer_size < 2) xtc->delay_buffer_size = 2; // Ensure at least 2 samples for stereo
    xtc->delay_buffer = (float*)calloc(xtc->delay_buffer_size, sizeof(float));
    xtc->delay_index = 0;
}*/
void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
	if (channels != 2) {
		return;
	}
	// 音速343m/sを基準に、スピーカー間距離から遅延を計算
	xtc->delay_samples = (int)(sample_rate * (speaker_distance_m / 343.0));
	xtc->attenuation = 0.4f; // デフォルト値
	xtc->delay_buffer_size = xtc->delay_samples * 2;
	if (xtc->delay_buffer_size < 2) {
		xtc->delay_buffer_size = 2;
	}
	xtc->delay_buffer = (float*)calloc(xtc->delay_buffer_size, sizeof(float));
	xtc->delay_index = 0;
}

void free_crosstalk_cancellation(CrosstalkCancel *xtc)
{
	if (xtc->delay_buffer) {
		free(xtc->delay_buffer);
		xtc->delay_buffer = NULL;
	}
}

void apply_crosstalk_cancellation(CrosstalkCancel *xtc, void *buffer, int frames, int channels, int format)
{
	if (channels != 2) {
		return;        // Only support stereo
	}

	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		float *data = (float*)buffer;
		float temp[frames * 2];
		memcpy(temp, data, frames * 2 * sizeof(float));

		// Process crosstalk cancellation
		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			// Store current samples in delay buffer
			xtc->delay_buffer[xtc->delay_index] = temp[idx];     // Left
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1]; // Right
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			// Apply crosstalk cancellation
			float delayed_left = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			float new_left = temp[idx] - xtc->attenuation * delayed_right;
			float new_right = temp[idx + 1] - xtc->attenuation * delayed_left;

			// Clipping prevention
			/*if (new_left > 1.0f) new_left = 1.0f;
			if (new_left < -1.0f) new_left = -1.0f;
			if (new_right > 1.0f) new_right = 1.0f;
			if (new_right < -1.0f) new_right = -1.0f;
			data[idx] = new_left;
			data[idx + 1] = new_right;*/
			data[idx] = fmaxf(fminf(new_left, 1.0f), -1.0f);
			data[idx + 1] = fmaxf(fminf(new_right, 1.0f), -1.0f);
		}
	} else { // SND_PCM_FORMAT_S16_LE
		/*int16_t *data = (int16_t*)buffer;
		float temp[frames * 2];
		float original_temp[frames * 2]; // Store original signal

		// Convert int16 to float for processing
		for (int i = 0; i < frames * 2; i++) {
		    original_temp[i] = data[i] / 32768.0f;
		}
		memcpy(temp, original_temp, frames * 2 * sizeof(float));

		// Process crosstalk cancellation
		for (int i = 0; i < frames; i++) {
		    int idx = i * 2;
		    int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

		    // Store current samples in delay buffer
		    xtc->delay_buffer[xtc->delay_index] = original_temp[idx];     // Left
		    xtc->delay_buffer[xtc->delay_index + 1] = original_temp[idx + 1]; // Right
		    xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

		    // Apply crosstalk cancellation
		    float delayed_left = xtc->delay_buffer[delay_idx];
		    float delayed_right = xtc->delay_buffer[delay_idx + 1];

		    temp[idx] = original_temp[idx] - xtc->attenuation * delayed_right;
		    temp[idx + 1] = original_temp[idx + 1] - xtc->attenuation * delayed_left;
		}

		for (int i = 0; i < frames * 2; i++) {
		    float val = temp[i] * 32767.0f; // Use 32767.0f to avoid overflow
		    if (val > 32767.0f) val = 32767.0f;
		    if (val < -32768.0f) val = -32768.0f;
		    data[i] = (int16_t)val;
		}*/
		int16_t *data = (int16_t*)buffer;
		float temp[frames * 2];
		for (int i = 0; i < frames * 2; i++) {
			temp[i] = data[i] / 32768.0f;
		}

		for (int i = 0; i < frames; i++) {
			int idx = i * 2;
			int delay_idx = (xtc->delay_index - xtc->delay_samples * 2 + xtc->delay_buffer_size) % xtc->delay_buffer_size;

			xtc->delay_buffer[xtc->delay_index] = temp[idx];
			xtc->delay_buffer[xtc->delay_index + 1] = temp[idx + 1];
			xtc->delay_index = (xtc->delay_index + 2) % xtc->delay_buffer_size;

			float delayed_left = xtc->delay_buffer[delay_idx];
			float delayed_right = xtc->delay_buffer[delay_idx + 1];

			temp[idx] = temp[idx] - xtc->attenuation * delayed_right;
			temp[idx + 1] = temp[idx + 1] - xtc->attenuation * delayed_left;
		}

		for (int i = 0; i < frames * 2; i++) {
			float val = temp[i] * 32767.0f;
			data[i] = (int16_t)fmaxf(fminf(val, 32767.0f), -32768.0f);
		}
	}
}

void print_test_mode_status(int phase, int flag, CrosstalkCancel *xtc, int sample_rate, int channels)
{
	static const char *phase_name[3] = {"Left channel", "Right channel", "Panning L->R"};
	tui_state_t ts = {0};
	ts.filename = phase_name[phase];
	ts.codec = "TEST";
	ts.rate = sample_rate;
	ts.channels = channels;
	ts.device = dev;
	ts.use_time = 0;
	ts.unit = "";
	ts.volume = volume;
	ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
	ts.xtc_atten = xtc->attenuation;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;
	ts.note = "[+/-] adjust attenuation";
	tui_render(&ts);
}

void play_test_mode(int format, int flag)
{
	const int sample_rate = 44100; // Standard sample rate
	const int channels = 2;        // Stereo
	const double duration = 5.0;   // 5 seconds per phase
	const int frames_per_phase = (int)(sample_rate * duration);
	const int total_phases = 3;    // Left, Right, Pan
	const int frequency = 400;

	AUDIO a;
	if (AUDIO_init(&a, dev, sample_rate, channels, FRAMES, 1, format)) {
		printf("Error: Failed to initialize ALSA for stereo output\n");
		return;
	}

	printf("Entering Test mode...\n");
	if (format == SND_PCM_FORMAT_FLOAT_LE) {
		printf("Format: 32-bit FLOAT\n");
	} else {
		printf("Format: 16-bit S16_LE\n");
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, sample_rate, channels);

	uint64_t global_sample_index = 0;
	int phase_sample_index = 0;
	int phase = 0;
	tui_open();
	print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
	while (1) {
		int k = key(&a, NULL);
		if (k) {
			if (k == 'q' || k == 0x1b) {
				break;
			}
			if (k == 'c') {
				flag ^= USE_CROSSTALK;
			}
			if (k == '+' || k == '=') {
				xtc.attenuation += 0.05f;
				if (xtc.attenuation > 1.0f) {
					xtc.attenuation = 1.0f;
				}
			}
			if (k == '-') {
				xtc.attenuation -= 0.05f;
				if (xtc.attenuation < 0.0f) {
					xtc.attenuation = 0.0f;
				}
			}
			print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
		}

		void *output_buffer = a.buffer;
		float *buffer_f32 = (float*)a.buffer;
		int16_t *buffer_s16 = (int16_t*)a.buffer;
		for (int i = 0; i < FRAMES; i++) {
			double t = (double)(global_sample_index + i) / sample_rate;

			int current_phase_sample = phase_sample_index + i;
			double left_amplitude, right_amplitude;

			if (phase == 0) { // Left channel only
				left_amplitude = 0.5;
				right_amplitude = 0.0;
			} else if (phase == 1) { // Right channel only
				left_amplitude = 0.0;
				right_amplitude = 0.5;
			} else { // Pan from left to right
				double pan = (double)current_phase_sample / frames_per_phase;
				if (pan > 1.0) {
					pan = 1.0;
				}
				left_amplitude = 0.5 * (1.0 - pan);
				right_amplitude = 0.5 * pan;
			}

			double left = left_amplitude * sin(2.0 * M_PI * frequency * t);
			double right = right_amplitude * sin(2.0 * M_PI * frequency * t);

			if (format == SND_PCM_FORMAT_FLOAT_LE) {
				buffer_f32[i * 2] = (float)left;
				buffer_f32[i * 2 + 1] = (float)right;
			} else {
				buffer_s16[i * 2] = (int16_t)(left * 32767.0f);
				buffer_s16[i * 2 + 1] = (int16_t)(right * 32767.0f);
			}
		}

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, output_buffer, FRAMES, channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		global_sample_index += FRAMES;
		phase_sample_index += FRAMES;
		if (phase_sample_index >= frames_per_phase) {
			phase_sample_index = 0;
			phase = (phase + 1) % total_phases;
			print_test_mode_status(phase, flag, &xtc, sample_rate, channels);
		}
	}
	tui_close();

	AUDIO_close(&a);
	free_crosstalk_cancellation(&xtc);
}

void display_progress(uint64_t current, uint64_t total, int is_time, const char *label)
{
	if (total == 0) {
		return;        // Skip if unknown
	}
	if (is_time) {
		int cur_sec = (int)(current % 60);
		int cur_min = (int)(current / 60);
		int tot_sec = (int)(total % 60);
		int tot_min = (int)(total / 60);
		printf("\r%s %02d:%02d / %02d:%02d", label, cur_min, cur_sec, tot_min, tot_sec);
	} else {
		printf("\r%s %llu / %llu bytes", label, (unsigned long long)current, (unsigned long long)total);
	}
	fflush(stdout);
}

void play_wav(char *name, int format, int flag)
{
	drwav wav;
	if (!drwav_init_file(&wav, name, NULL)) return;

	OutRouter router;
	if (outr_init(&router, dev, wav.sampleRate, wav.channels, format, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		drwav_uninit(&wav);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, wav.sampleRate, wav.channels);

	float *pcm_buf = (float*)malloc(FRAMES * wav.channels * sizeof(float));

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WAV->DSD(mon)" : "WAV->DSD(DoP)") : (format ? "WAV/F32" : "WAV");
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : 16;
	ts.channels = wav.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = wav.totalPCMFrameCount > 0 ? (double)wav.totalPCMFrameCount / wav.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n; // numberOfSamplesActuallyDecoded
	while ((n = drwav_read_pcm_frames_f32(&wav, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, wav.channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k=='e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WAV->DSD(mon)" : "WAV->DSD(DoP)") : (format ? "WAV/F32" : "WAV");
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * wav.sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (wav.totalPCMFrameCount > 0 && (uint64_t)target > wav.totalPCMFrameCount) {
				target = (int64_t)wav.totalPCMFrameCount;
			}
			if (drwav_seek_to_pcm_frame(&wav, (drwav_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / wav.sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / wav.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		ts.note = (n != FRAMES) ? "! short read" : NULL;
		tui_render(&ts);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drwav_uninit(&wav);
	free_crosstalk_cancellation(&xtc);
}

#ifdef USE_FOXEN_FLAC

#include <stdlib.h>

#define FX_IN_BUF_SIZE  8192
/* One Subset block × max channels; covers all common FLAC files. */
#define FX_OUT_SAMPLES  (FLAC_SUBSET_MAX_BLOCK_SIZE * FLAC_MAX_CHANNEL_COUNT)

/* Seek to target_frame by rewinding the file and decoding forward.
 * foxen-flac has no native seek; this is O(target) but acceptable for
 * a music player where seeks are at most a few seconds at a time. */
static int fx_seek_to_frame(FILE *f, fx_flac_t *flac, uint64_t target_frame,
                            uint32_t channels)
{
	rewind(f);
	fx_flac_reset(flac);

	uint8_t in_buf[FX_IN_BUF_SIZE];
	uint32_t in_fill = 0;
	int32_t *out_buf = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	if (!out_buf) return 0;

	uint64_t discarded = 0;
	uint64_t target_samples = target_frame * channels;
	fx_flac_state_t state = FLAC_INIT;

	while (discarded < target_samples) {
		if (in_fill < FX_IN_BUF_SIZE && !feof(f)) {
			size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
			in_fill += (uint32_t)rd;
		}
		if (in_fill == 0) break;

		uint32_t in_len = in_fill;
		uint32_t out_len = FX_OUT_SAMPLES;
		state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
		memmove(in_buf, in_buf + in_len, in_fill - in_len);
		in_fill -= in_len;

		if (state == FLAC_ERR) break;
		discarded += out_len;
		if (in_len == 0 && out_len == 0) break; /* no progress guard */
	}

	free(out_buf);
	return state != FLAC_ERR;
}

void play_flac(char *name, int format, int flag)
{
	FILE *f = fopen(name, "rb");
	if (!f) {
		fprintf(stderr, "Failed to open FLAC: %s\n", name);
		return;
	}

	fx_flac_t *flac = FX_FLAC_ALLOC_DEFAULT();
	if (!flac) { fclose(f); return; }
	fx_flac_reset(flac);

	int32_t *out_buf = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	if (!out_buf) { free(flac); fclose(f); return; }

	uint8_t in_buf[FX_IN_BUF_SIZE];
	uint32_t in_fill = 0;

	/* Phase 1: pump decoder until stream info is available. */
	fx_flac_state_t state = FLAC_INIT;
	while (state < FLAC_END_OF_METADATA) {
		if (in_fill < FX_IN_BUF_SIZE) {
			size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
			in_fill += (uint32_t)rd;
		}
		if (in_fill == 0) {
			fprintf(stderr, "FLAC: EOF before metadata: %s\n", name);
			goto done;
		}
		uint32_t in_len = in_fill;
		uint32_t out_len = FX_OUT_SAMPLES;
		state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
		memmove(in_buf, in_buf + in_len, in_fill - in_len);
		in_fill -= in_len;
		if (state == FLAC_ERR) {
			fprintf(stderr, "FLAC: metadata error: %s\n", name);
			goto done;
		}
	}

	{
	uint32_t sample_rate = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_SAMPLE_RATE);
	uint32_t channels    = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_N_CHANNELS);
	uint32_t bits        = (uint32_t)fx_flac_get_streaminfo(flac, FLAC_KEY_SAMPLE_SIZE);
	int64_t  n_samples   = fx_flac_get_streaminfo(flac, FLAC_KEY_N_SAMPLES);

	OutRouter router;
	if (outr_init(&router, dev, sample_rate, channels, format, (flag & USE_DSD_ENCODE) ? 1 : 0)) goto done;

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, sample_rate, channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : bits;
	ts.channels = channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = n_samples > 0 ? (double)n_samples / sample_rate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	/* Accumulation buffer: foxen-flac decodes one block at a time
	 * (up to FX_OUT_SAMPLES interleaved samples).  We drain it FRAMES
	 * frames at a time to match the ALSA/DSD period size. */
	int32_t *pcm_acc = malloc(FX_OUT_SAMPLES * sizeof(int32_t));
	float *pcm_f32 = malloc(FRAMES * channels * sizeof(float));
	if (!pcm_acc || !pcm_f32) { outr_close(&router); free_crosstalk_cancellation(&xtc); free(pcm_acc); free(pcm_f32); goto done; }
	uint32_t acc_fill = 0;
	int eof = 0;

	uint64_t frame_pos = 0;
	uint32_t frames_ch = FRAMES * channels;

	tui_open();
	for (;;) {
		/* Refill accumulation buffer. */
		while (!eof && acc_fill < frames_ch) {
			if (in_fill < FX_IN_BUF_SIZE && !feof(f)) {
				size_t rd = fread(in_buf + in_fill, 1, FX_IN_BUF_SIZE - in_fill, f);
				in_fill += (uint32_t)rd;
			}
			if (in_fill == 0) { eof = 1; break; }

			uint32_t in_len = in_fill;
			uint32_t out_len = FX_OUT_SAMPLES;
			state = fx_flac_process(flac, in_buf, &in_len, out_buf, &out_len);
			memmove(in_buf, in_buf + in_len, in_fill - in_len);
			in_fill -= in_len;

			if (state == FLAC_ERR) { eof = 1; break; }
			if (out_len > 0) {
				uint32_t space = FX_OUT_SAMPLES - acc_fill;
				if (out_len > space) out_len = space;
				memcpy(pcm_acc + acc_fill, out_buf, out_len * sizeof(int32_t));
				acc_fill += out_len;
			}
			if (in_len == 0 && out_len == 0) {
				if (feof(f) && in_fill == 0) eof = 1;
				break; /* no progress — try again after draining ALSA */
			}
		}

		if (acc_fill < frames_ch) break;

		/* foxen-flac left-shifts samples to fill all 32 bits; always
		 * convert down to float first so both the PCM and DSD output
		 * paths can share the same downstream code. */
		for (uint32_t i = 0; i < frames_ch; i++)
			pcm_f32[i] = (float)pcm_acc[i] * (1.0f / 2147483648.0f);

		memmove(pcm_acc, pcm_acc + frames_ch, (acc_fill - frames_ch) * sizeof(int32_t));
		acc_fill -= frames_ch;

		if (flag & USE_CROSSTALK)
			apply_crosstalk_cancellation(&xtc, pcm_f32, FRAMES, channels, SND_PCM_FORMAT_FLOAT_LE);

		outr_feed_float(&router, pcm_f32, FRAMES);

		frame_pos += FRAMES;
		ts.cur = (double)frame_pos / sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
		} else if (k == 'e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * sample_rate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)frame_pos + delta;
			if (target < 0) target = 0;
			if (n_samples > 0 && target > n_samples) target = n_samples;
			if (fx_seek_to_frame(f, flac, (uint64_t)target, channels)) {
				frame_pos = (uint64_t)target;
				acc_fill = 0;
				in_fill = 0;
				eof = 0;
				ts.cur = (double)frame_pos / sample_rate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}
	}
	tui_close();

	outr_close(&router);
	free_crosstalk_cancellation(&xtc);
	free(pcm_acc);
	free(pcm_f32);
	}
done:
	free(out_buf);
	free(flac);
	fclose(f);
}

#else  /* !USE_FOXEN_FLAC — use dr_flac */

void play_flac(char *name, int format, int flag)
{
	drflac *flac = drflac_open_file(name, NULL);
	if (!flac) {
		fprintf(stderr, "Failed to open FLAC: %s\n", name);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, flac->sampleRate, flac->channels, format, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		drflac_close(flac);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, flac->sampleRate, flac->channels);

	float *pcm_buf = (float*)malloc(FRAMES * flac->channels * sizeof(float));

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : flac->bitsPerSample;
	ts.channels = flac->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = flac->totalPCMFrameCount > 0 ? (double)flac->totalPCMFrameCount / flac->sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n; // numberOfSamplesActuallyDecoded
	while ((n = drflac_read_pcm_frames_f32(flac, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, flac->channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k=='e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "FLAC->DSD(mon)" : "FLAC->DSD(DoP)") : "FLAC";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * flac->sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (flac->totalPCMFrameCount > 0 && (uint64_t)target > flac->totalPCMFrameCount) {
				target = (int64_t)flac->totalPCMFrameCount;
			}
			if (drflac_seek_to_pcm_frame(flac, (drflac_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / flac->sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / flac->sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drflac_close(flac);
	free_crosstalk_cancellation(&xtc);
}

#endif /* USE_FOXEN_FLAC */

// Handles both .dsf (Sony DSF) and .dff (Philips DSDIFF) files; dsd.h
// auto-detects the container format from the file's magic bytes.
void play_dsf(char *name, int format, int flag)
{
	FILE *f = fopen(name, "rb");
	if (!f) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	// Initialize DSD decoder from file stream
	DSDDecoder *decoder = dsd_decoder_init_file(f);
	if (!decoder) {
		printf("Error: failed to initialize DSD decoder for `%s`\n", name);
		fclose(f);
		return;
	}

	AUDIO a;
	if (AUDIO_init(&a, dev, decoder->sample_rate_pcm, decoder->channels, FRAMES, 1, format)) {
		dsd_decoder_free(decoder);
		fclose(f);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, decoder->sample_rate_pcm, decoder->channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = (decoder->file_type == DSD_FILE_DFF) ? "DFF" : "DSF";
	ts.rate = decoder->sample_rate_pcm;
	ts.bits = format == SND_PCM_FORMAT_FLOAT_LE ? 32 : 16;
	ts.channels = decoder->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = decoder->totalPCMFrameCount > 0 ?
	           (double)decoder->totalPCMFrameCount / decoder->sample_rate_pcm : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t frames_played = 0;
	tui_open();
	size_t n;
	while ((n = dsd_decoder_read_pcm_frames(decoder, a.frames, a.buffer, format)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, a.buffer, n, decoder->channels, format);
		}

		AUDIO_play0(&a);
		AUDIO_wait(&a, 100);

		int k = key(&a, &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = (decoder->file_type == DSD_FILE_DFF) ? "seek not supported for DFF" : "seek not supported for DSF";
			tui_render(&ts);
		} else if (k) {
			break;
		}

		frames_played += n;
		ts.cur = (double)frames_played / decoder->sample_rate_pcm;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		ts.note = (n != a.frames) ? "! short read (end of file)" : NULL;
		tui_render(&ts);
	}
	tui_close();

	AUDIO_close(&a);
	dsd_decoder_free(decoder);
	fclose(f);
	free_crosstalk_cancellation(&xtc);
}

#ifdef DR_MP3_IMPLEMENTATION
int play_mp3(char *name, int format, int flag)
{
	drmp3 mp3;
	if (!drmp3_init_file(&mp3, name, NULL)) {
		printf("Error: not a valid MP3 audio file!\n");
		return 1;
	}

	OutRouter router;
	if (outr_init(&router, dev, mp3.sampleRate, mp3.channels, format, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		drmp3_uninit(&mp3);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, mp3.sampleRate, mp3.channels);

	float *pcm_buf = (float*)malloc(FRAMES * mp3.channels * sizeof(float));

	drmp3_uint64 totalPCMFrameCount = drmp3_get_pcm_frame_count(&mp3);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "MP3->DSD(mon)" : "MP3->DSD(DoP)") : "MP3";
	ts.rate = outr_output_rate(&router);
	ts.bits = format ? 32 : 16;
	ts.channels = mp3.channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = totalPCMFrameCount > 0 ? (double)totalPCMFrameCount / mp3.sampleRate : 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	size_t n;
	while ((n = drmp3_read_pcm_frames_f32(&mp3, FRAMES, pcm_buf)) > 0) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, pcm_buf, (int)n, mp3.channels, SND_PCM_FORMAT_FLOAT_LE);
		}

		outr_feed_float(&router, pcm_buf, n);

		int k = key(outr_audio(&router), &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k=='e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "MP3->DSD(mon)" : "MP3->DSD(DoP)") : "MP3";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * mp3.sampleRate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (totalPCMFrameCount > 0 && (uint64_t)target > totalPCMFrameCount) {
				target = (int64_t)totalPCMFrameCount;
			}
			if (drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / mp3.sampleRate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}

		c += n;
		ts.cur = (double)c / mp3.sampleRate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	free(pcm_buf);
	outr_close(&router);
	drmp3_uninit(&mp3);
	free_crosstalk_cancellation(&xtc);
	return 0;
}
#else
int play_mp3(char *name, int format, int flag)
{
	short sample_buf[MP3_MAX_SAMPLES_PER_FRAME];
	int len;
	void *file_data = preload(name, &len);
	unsigned char *stream_pos = (unsigned char *)file_data;
	int bytes_left = len - 100;

	mp3_info_t info;
	mp3_decoder_t mp3 = mp3_create();
	int frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, &info);
	if (!frame_size) {
		printf("Error: not a valid MP3 audio file!\n");
		munmap(file_data, len);
		return 1;
	}

	printf("%dHz %dch\n", info.sample_rate, info.channels);
	OutRouter router;
	if (outr_init(&router, dev, info.sample_rate, info.channels, 0, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		munmap(file_data, len);
		return 1;
	}
	if (router.use_dsd) {
		// このビルド(minimp3フォールバック)にはTUIが無いため、リアルタイムの
		// 'e'キー切替には未対応。-eの指定どおりDSD(DoP)出力に固定で再生する。
		printf("DSD encode (DoP) output enabled for this track\n");
	}

	CrosstalkCancel xtc;
	if (flag & USE_CROSSTALK) {
		init_crosstalk_cancellation(&xtc, info.sample_rate, info.channels);
		printf(" with Crosstalk Cancellation\n");
	}

	int c = 0;
	printf("\e[?25l");
	while ((bytes_left >= 0) && (frame_size > 0) && !key(outr_audio(&router), NULL)) {
		printf("\r%d", c);

		int n_frames = info.audio_bytes / 2 / info.channels;

		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, sample_buf, n_frames, info.channels, 0);
		}
		stream_pos += frame_size;
		bytes_left -= frame_size;

		if (router.use_dsd) {
			dsdaccum_push_s16(&router.accum, sample_buf, (size_t)n_frames);
			dsdaccum_drain(&router.accum, &router.dsd);
		} else {
			AUDIO_play(&router.pcm, (char*)sample_buf, n_frames);
			AUDIO_wait(&router.pcm, 100);
		}

		c += frame_size;
		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}
	printf("\e[?25h");

	outr_close(&router);
	mp3_free(mp3);
	munmap(file_data, len);
	if (flag & USE_CROSSTALK) {
		free_crosstalk_cancellation(&xtc);
	}
	return 0;
}
#endif

void play_ogg(char *name, int flag)
{
	int n, num_c, error;
	short outputs[FRAMES*2*100];

	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);
	if (!v) {
		printf("Error: cannot open `%s`\n", name);
		return;
	}

	OutRouter router;
	if (outr_init(&router, dev, v->sample_rate, v->channels, 0, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		stb_vorbis_close(v);
		return;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, v->sample_rate, v->channels);

	// stb_vorbis doesn't give us a cheap total-sample count up front, so this
	// stream is shown with elapsed time only (no total / no progress bar fill).
	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "OGG->DSD(mon)" : "OGG->DSD(DoP)") : "OGG";
	ts.rate = outr_output_rate(&router);
	ts.channels = v->channels;
	ts.device = dev;
	ts.use_time = 1;
	ts.total = 0.0;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	uint64_t c = 0;
	tui_open();
	while ((n = stb_vorbis_get_frame_short_interleaved(v, v->channels, outputs, FRAMES*100))) {
		if (flag & USE_CROSSTALK) {
			apply_crosstalk_cancellation(&xtc, outputs, n, v->channels, 0);
		}

		if (router.use_dsd) {
			dsdaccum_push_s16(&router.accum, outputs, (size_t)n);
			dsdaccum_drain(&router.accum, &router.dsd);
		} else {
			AUDIO_play(&router.pcm, (char*)outputs, n);
			AUDIO_wait(&router.pcm, 100);
		}

		int k = key(outr_audio(&router), &ts);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k=='e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "OGG->DSD(mon)" : "OGG->DSD(DoP)") : "OGG";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			int64_t delta = (int64_t)SEEK_SECONDS * v->sample_rate * (k == KEY_RIGHT ? 1 : -1);
			int64_t target = (int64_t)c + delta;
			if (target < 0) {
				target = 0;
			}
			if (stb_vorbis_seek(v, (unsigned int)target)) {
				c = (uint64_t)target;
				ts.cur = (double)c / v->sample_rate;
				ts.note = (k == KEY_RIGHT) ? ">> +10s" : "<< -10s";
				tui_render(&ts);
			}
		} else if (k) {
			break;
		}
		c += n;
		ts.cur = (double)c / v->sample_rate;
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	outr_close(&router);
	stb_vorbis_close(v);
	free_crosstalk_cancellation(&xtc);
}

#define MAX_WMA_FRAME_LEN 4096
#define MAX_WMA_SUBFRAMES 16
#define MAX_PCM_BUFFER_SIZE (MAX_WMA_SUBFRAMES * MAX_WMA_FRAME_LEN * 8 * sizeof(short))  // 8 channels max, ~1MB safe
int play_wma(char *name, int flag)
{
	void *file_data = NULL;
	unsigned char *stream_pos;
	short *sample_buf = NULL;
	int bytes_left;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	int len = lseek(fd, 0, SEEK_END);
	if (len <= 0) {
		fprintf(stderr, "Error: invalid file size\n");
		close(fd);
		return 1;
	}

	lseek(fd, 0, SEEK_SET);
	file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (file_data == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	stream_pos = (unsigned char *)file_data;
	bytes_left = len;

	CodecContext cc = {0};
	cc.priv_data = calloc(1, sizeof(WMADecodeContext));
	if (!cc.priv_data) {
		fprintf(stderr, "Error: failed to allocate WMA context\n");
		munmap(file_data, len);
		return 1;
	}

	if (parse_wma_header(stream_pos, bytes_left, &cc) != 0) {
		fprintf(stderr, "Error: failed to parse WMA header\n");
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	WMADecodeContext *s = (WMADecodeContext *)cc.priv_data;
	if (wma_decode_init_fixed(&cc) < 0) {
		fprintf(stderr, "Error: failed to initialize WMA decoder\n");
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	size_t pcm_buffer_size = s->nb_channels * s->frame_len * MAX_WMA_SUBFRAMES * sizeof(short);
	if (pcm_buffer_size == 0 || pcm_buffer_size > MAX_PCM_BUFFER_SIZE || s->nb_channels == 0 || s->frame_len == 0) {
		fprintf(stderr, "Error: invalid PCM buffer parameters: nb_channels=%d, frame_len=%d, pcm_buffer_size=%zu\n",
		        s->nb_channels, s->frame_len, pcm_buffer_size);
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	sample_buf = malloc(pcm_buffer_size);
	if (!sample_buf) {
		fprintf(stderr, "Error: failed to allocate PCM buffer of size %zu bytes\n", pcm_buffer_size);
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(cc.priv_data);
		return 1;
	}

	OutRouter router;
	if (outr_init(&router, dev, cc.sample_rate, cc.channels, 0, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
		fprintf(stderr, "Error: failed to initialize audio\n");
		wma_decode_end(&cc);
		munmap(file_data, len);
		free(sample_buf);
		free(cc.priv_data);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, cc.sample_rate, cc.channels);

	tui_state_t ts = {0};
	ts.track_index = track_index;
	ts.track_total = track_total;
	ts.filename = get_basename(name);
	char dirbuf[PATH_MAX];
	get_dirpart(name, dirbuf, sizeof(dirbuf));
	ts.dir = dirbuf;
	ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WMA->DSD(mon)" : "WMA->DSD(DoP)") : "WMA";
	ts.rate = outr_output_rate(&router);
	ts.channels = cc.channels;
	ts.device = dev;
	ts.use_time = 0; // decoded frame timing isn't tracked; show raw byte progress instead
	ts.unit = "bytes";
	ts.total_raw = (uint64_t)len;
	ts.volume = volume;
	ts.loop_mode = loop_mode;
	ts.format_filter = fmt_filter;

	tui_open();
	while (bytes_left > 0) {
		int out_size = 0;
		int frame_size = wma_decode_superframe(&cc, sample_buf, &out_size, stream_pos, bytes_left);
		if (frame_size <= 0 || out_size <= 0) {
			fprintf(stderr, "Error: failed to decode WMA frame, frame_size=%d, out_size=%d, bytes_left=%d\n",
			        frame_size, out_size, bytes_left);
			break;
		}

		if (out_size > (int)pcm_buffer_size) {
			fprintf(stderr, "Error: decoded output size %d exceeds buffer size %zu\n", out_size, pcm_buffer_size);
			break;
		}

		int frames = out_size / (sizeof(short) * cc.channels);
		if (frames <= 0 || frames > MAX_WMA_FRAME_LEN || cc.channels == 0) {
			fprintf(stderr, "Error: invalid frame count %d, channels=%d\n", frames, cc.channels);
			break;
		}

		if (flag & USE_CROSSTALK) {
			if (!sample_buf) {
				fprintf(stderr, "Error: sample_buf is NULL\n");
				break;
			}
			apply_crosstalk_cancellation(&xtc, sample_buf, frames, cc.channels, 0);
		}

		if (router.use_dsd) {
			dsdaccum_push_s16(&router.accum, sample_buf, (size_t)frames);
			dsdaccum_drain(&router.accum, &router.dsd);
		} else {
			AUDIO_play(&router.pcm, (char*)sample_buf, frames);
			AUDIO_wait(&router.pcm, 100);
		}

		stream_pos += frame_size;
		bytes_left -= frame_size;

		int k = key(outr_audio(&router), &ts);
		if (k == 'c') {
			flag ^= USE_CROSSTALK;
		} else if (k == 'e') {
			outr_toggle(&router);
			ts.codec = router.use_dsd ? (router.dsd.monitor_mode ? "WMA->DSD(mon)" : "WMA->DSD(DoP)") : "WMA";
			ts.rate = outr_output_rate(&router);
			ts.note = outr_status_note(&router);
			tui_render(&ts);
		} else if (k == KEY_LEFT || k == KEY_RIGHT) {
			ts.note = "seek not supported for WMA";
			tui_render(&ts);
		} else if (k) {
			break;
		}

		ts.cur_raw = (uint64_t)(len - bytes_left);
		ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
		ts.xtc_atten = xtc.attenuation;
		tui_render(&ts);
	}
	tui_close();

	outr_close(&router);
	wma_decode_end(&cc);
	munmap(file_data, len);
	free(sample_buf);
	free(cc.priv_data);
	free_crosstalk_cancellation(&xtc);
	return 0;
}

/*int play_aac(char *name, int flag)
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

    int samplerate, channels;
    file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels);
    if (!file_data) {
        printf("Error: cannot read AAC data\n");
        close(fd);
        return 1;
    }
    stream_pos = file_data;

    AACFrameInfo info;
    memset(&info, 0, sizeof(AACFrameInfo));
    info.nChans = channels;
    info.sampRateCore = samplerate;
    info.profile = AAC_PROFILE_LC;

    HAACDecoder aac = AACInitDecoder();
    AACSetRawBlockParams(aac, 0, &info);

    int output_samplerate = info.sampRateCore;
    int sbr_enabled = 0;
    if (output_samplerate <= 24000) {
        output_samplerate *= 2;
        sbr_enabled = 1;
    }

    printf("%dHz (output: %dHz) %dch\n", info.sampRateCore, output_samplerate, info.nChans);

    AUDIO a;
    if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {
        free(file_data);
        close(fd);
        AACFreeDecoder(aac);
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

    printf("\e[?25l");
    while (bytes_left > 0) {
        int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
        printf("\r%d %d", (int)(stream_pos - file_data), bytes_left);
        if (!r) {
            AACGetLastFrameInfo(aac, &info);
            if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, sample_buf, AAC_MAX_NSAMPS, info.nChans, 0);
            AUDIO_play(&a, (char*)sample_buf, AAC_MAX_NSAMPS);
            AUDIO_wait(&a, 100);
        } else {
            printf("\nAAC decode error %d, attempting resync\n", r);
            if (!sbr_enabled && info.sampRateCore <= 24000) {
                printf("Trying HE-AAC with SBR\n");
                info.sampRateCore *= 2;
                info.profile = 5; // HE-AAC
                AACFreeDecoder(aac);
                aac = AACInitDecoder();
                AACSetRawBlockParams(aac, 0, &info);
                stream_pos = file_data;
                bytes_left = *(&bytes_left);
                continue;
            }
            int nextSync = AACFindSyncWord(stream_pos, bytes_left);
            if (nextSync >= 0) {
                stream_pos += nextSync;
                bytes_left -= nextSync;
                continue;
            } else {
                printf("Failed to resync, stopping\n");
                break;
            }
        }

        int k = key(&a);
        if (k=='c') flag ^= USE_CROSSTALK;
        else if (k) break;
    }
    printf("\e[?25h");

    AUDIO_close(&a);
    AACFreeDecoder(aac);
    free(file_data);
    close(fd);
    free_crosstalk_cancellation(&xtc);
    return 0;
}*/
/*int play_aac(char *name, int flag)
{
	unsigned char *file_data;
	unsigned char *stream_pos;
	short sample_buf[AAC_BUF_SIZE*2];
	int bytes_left;
	int max_resync_attempts = 5;
	int resync_attempts = 0;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		printf("Error: cannot open `%s`\n", name);
		return 1;
	}

	int samplerate, channels, profile;
	file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels, &profile);
	if (!file_data) {
		printf("Error: cannot read AAC data\n");
		close(fd);
		return 1;
	}
	stream_pos = file_data;

	AACFrameInfo info;
	memset(&info, 0, sizeof(AACFrameInfo));
	info.nChans = channels;
	info.sampRateCore = samplerate;
	info.profile = profile;

	HAACDecoder aac = AACInitDecoder();
	AACSetRawBlockParams(aac, 0, &info);

	int output_samplerate = samplerate;
	int sbr_enabled = (profile == 5);
	if (sbr_enabled) {
	    output_samplerate *= 2;
	} else if (samplerate <= 24000) {
	    // Assume potential HE-AAC if low rate and not detected
	    sbr_enabled = 1;
	    info.profile = 5;
	    output_samplerate *= 2;
	    printf("Assuming potential HE-AAC with SBR, output samplerate: %dHz\n", output_samplerate);
	}
	printf("%dHz (output: %dHz) %dch\n", info.sampRateCore, output_samplerate, info.nChans);

	AUDIO a;
	if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {
		printf("Error: failed to initialize ALSA with %dHz, %dch\n", output_samplerate, info.nChans);
		free(file_data);
		close(fd);
		AACFreeDecoder(aac);
		return 1;
	}

	CrosstalkCancel xtc;
	init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

	printf("\e[?25l");
	while (bytes_left > 0) {
		int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
		if (verbose) {
			printf("\rDecoded %d bytes, %d bytes left, result=%d\n", (int)(stream_pos - file_data), bytes_left, r);
		} else {
			printf("\r%d/%d", (int)(stream_pos - file_data), bytes_left + (int)(stream_pos - file_data));
		}
		if (!r) {
			resync_attempts = 0; // Reset on successful decode
			AACGetLastFrameInfo(aac, &info);
			if (verbose) {
				printf("Frame: %d samples, %d channels, %dHz\n", AAC_MAX_NSAMPS, info.nChans, info.sampRateCore);
			}
			if (flag & USE_CROSSTALK) {
				apply_crosstalk_cancellation(&xtc, sample_buf, AAC_MAX_NSAMPS, info.nChans, 0);
			}
			AUDIO_play(&a, (char*)sample_buf, AAC_MAX_NSAMPS);
			AUDIO_wait(&a, 100);
		} else {
			printf("\nAAC decode error %d, attempting resync (%d/%d)\n", r, resync_attempts + 1, max_resync_attempts);
			if (!sbr_enabled && info.sampRateCore <= 24000) {
				printf("Trying HE-AAC with SBR\n");
				info.sampRateCore *= 2;
				info.profile = 5; // HE-AAC
				AACFreeDecoder(aac);
				aac = AACInitDecoder();
				AACSetRawBlockParams(aac, 0, &info);
				stream_pos = file_data;
				bytes_left = *(&bytes_left);
				resync_attempts = 0;
				// Reinitialize ALSA with new sample rate
				AUDIO_close(&a);
				if (AUDIO_init(&a, dev, info.sampRateCore, info.nChans, FRAMES, 1, 0)) {
					printf("Error: failed to reinitialize ALSA with %dHz\n", info.sampRateCore);
					free(file_data);
					close(fd);
					AACFreeDecoder(aac);
					free_crosstalk_cancellation(&xtc);
					return 1;
				}
				continue;
			}
			int nextSync = AACFindSyncWord(stream_pos, bytes_left);
			if (nextSync >= 0) {
				stream_pos += nextSync;
				bytes_left -= nextSync;
				resync_attempts = 0;
				continue;
			} else if (++resync_attempts < max_resync_attempts) {
				stream_pos += 1;
				bytes_left -= 1;
				if (verbose) {
					printf("Skipping 1 byte, %d bytes left\n", bytes_left);
				}
				continue;
			} else {
				printf("Failed to resync after %d attempts, stopping\n", max_resync_attempts);
				break;
			}
		}

		int k = key(&a);
		if (k=='c') {
			flag ^= USE_CROSSTALK;
		} else if (k) {
			break;
		}
	}
	printf("\e[?25h");

	AUDIO_close(&a);
	AACFreeDecoder(aac);
	free(file_data);
	close(fd);
	free_crosstalk_cancellation(&xtc);
	return 0;
}*/
int play_aac(char *name, int flag)
{
    unsigned char *file_data;
    unsigned char *stream_pos;
    short sample_buf[AAC_BUF_SIZE * 2];
    int bytes_left;
    int max_resync_attempts = 5;
    int resync_attempts = 0;

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        printf("Error: cannot open `%s`\n", name);
        return 1;
    }

    int samplerate, channels, profile;
    file_data = uaac_extract_aac(fd, &bytes_left, &samplerate, &channels, &profile);
    if (!file_data) {
        printf("Error: cannot read AAC data\n");
        close(fd);
        return 1;
    }
    stream_pos = file_data;

    AACFrameInfo info;
    memset(&info, 0, sizeof(AACFrameInfo));
    info.nChans = channels;
    info.sampRateCore = samplerate;
    info.profile = profile;

    HAACDecoder aac = AACInitDecoder();
    AACSetRawBlockParams(aac, 0, &info);

    int output_samplerate = samplerate;
    int sbr_enabled = (profile == 5);
    if (sbr_enabled) {
        output_samplerate *= 2;
    } else if (samplerate <= 24000) {
        sbr_enabled = 1;
        info.profile = 5;
        output_samplerate *= 2;
        printf("Assuming potential HE-AAC with SBR, output samplerate: %dHz\n", output_samplerate);
    }
    OutRouter router;
    if (outr_init(&router, dev, output_samplerate, info.nChans, 0, (flag & USE_DSD_ENCODE) ? 1 : 0)) {
        printf("Error: failed to initialize ALSA with %dHz, %dch\n", output_samplerate, info.nChans);
        free(file_data);
        close(fd);
        AACFreeDecoder(aac);
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);

    tui_state_t ts = {0};
    ts.track_index = track_index;
    ts.track_total = track_total;
    ts.filename = get_basename(name);
    char dirbuf[PATH_MAX];
    get_dirpart(name, dirbuf, sizeof(dirbuf));
    ts.dir = dirbuf;
    ts.codec = router.use_dsd
        ? (router.dsd.monitor_mode
           ? (sbr_enabled ? "AAC/SBR->DSD(mon)" : "AAC->DSD(mon)")
           : (sbr_enabled ? "AAC/SBR->DSD(DoP)" : "AAC->DSD(DoP)"))
        : (sbr_enabled ? "AAC/SBR" : "AAC");
    ts.rate = outr_output_rate(&router);
    ts.channels = info.nChans;
    ts.device = dev;
    ts.use_time = 0; // AAC is decoded from a raw byte stream; show byte progress instead of time
    ts.unit = "bytes";
    ts.total_raw = (uint64_t)bytes_left;
    ts.volume = volume;
    ts.loop_mode = loop_mode;
    ts.format_filter = fmt_filter;

    tui_open();
    while (bytes_left > 0) {
        int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
        if (verbose) {
            printf("\rDecoded %d bytes, %d bytes left, result=%d\n", (int)(stream_pos - file_data), bytes_left, r);
        }
        if (!r) {
            resync_attempts = 0; // Reset on successful decode
            AACGetLastFrameInfo(aac, &info);
            int samples_per_frame = sbr_enabled ? 2048 : 1024; // Samples per channel
            if (verbose) {
                printf("Frame: %d samples/channel, %d channels, %dHz\n", samples_per_frame, info.nChans, info.sampRateCore);
            }
            int frames = samples_per_frame; // Samples per channel
            if (flag & USE_CROSSTALK) {
                apply_crosstalk_cancellation(&xtc, sample_buf, frames, info.nChans, 0);
            }
            if (router.use_dsd) {
                dsdaccum_push_s16(&router.accum, sample_buf, (size_t)frames);
                dsdaccum_drain(&router.accum, &router.dsd);
            } else {
                AUDIO_play(&router.pcm, (char*)sample_buf, frames);
                AUDIO_wait(&router.pcm, 100);
            }
        } else {
            ts.note = "AAC decode error, stopping";
            tui_render(&ts);
            // For raw AAC (MP4), do not attempt ADTS-based resync—just stop on error.
            // If this is an ADTS file (.aac), you could add conditional resync logic here.
            break;
        }

        int k = key(outr_audio(&router), &ts);
        if (k == 'c') {
            flag ^= USE_CROSSTALK;
        } else if (k == 'e') {
            outr_toggle(&router);
            ts.codec = router.use_dsd
                ? (router.dsd.monitor_mode
                   ? (sbr_enabled ? "AAC/SBR->DSD(mon)" : "AAC->DSD(mon)")
                   : (sbr_enabled ? "AAC/SBR->DSD(DoP)" : "AAC->DSD(DoP)"))
                : (sbr_enabled ? "AAC/SBR" : "AAC");
            ts.rate = outr_output_rate(&router);
            ts.note = outr_status_note(&router);
            tui_render(&ts);
        } else if (k == KEY_LEFT || k == KEY_RIGHT) {
            ts.note = "seek not supported for AAC";
            tui_render(&ts);
        } else if (k) {
            break;
        }

        ts.cur_raw = (uint64_t)(stream_pos - file_data);
        ts.xtc_on = (flag & USE_CROSSTALK) ? 1 : 0;
        ts.xtc_atten = xtc.attenuation;
        tui_render(&ts);
    }
    tui_close();

    outr_close(&router);
    AACFreeDecoder(aac);
    free(file_data);
    close(fd);
    free_crosstalk_cancellation(&xtc);
    return 0;
}

// Extracts the parent directory portion of path into dir (size bytes).
// "music/rock/song.flac" -> "music/rock", "song.flac" -> "."
static void get_dirpart(const char *path, char *dir, size_t size)
{
	strncpy(dir, path, size - 1);
	dir[size - 1] = '\0';
	char *slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	else strcpy(dir, ".");
}

void play_dir(char *name, char *type, char *regexp, int flag)
{
	char path[1024], ext[10];
	int num, back=0;
	int format = flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;

	do {
		LS_LIST *ls = ls_dir(name, flag, &num);
		for (int i=0; i<num; i++) {
			char *e = findExt(ls[i].d_name);
			if (type) {
				if (!strstr(e, type)) {
					continue;
				}
			}
			// Interactive filter set with the 'f' key (see key()); re-read live
			// so toggling it mid-playlist takes effect from the next track.
			if (fmt_filter) {
				if (!strstr(e, fmt_filter)) {
					continue;
				}
			}
			if (regexp) {
				const char *error;
				//Reprog *p = regcomp(regexp, 0, &error);
				Reprog *p = regcomp(regexp, REG_ICASE, &error);
				if (!p) {
					fprintf(stderr, "regcomp: %s\n", error);
					return;
				}
				Resub m;
				if (regexec(p, ls[i].d_name, &m, 0)) {
					continue;
				}
			}

			track_index = i + 1;
			track_total = num;
			snprintf(path, 1024, "%s", ls[i].d_name);
			if (access(ls[i].d_name, F_OK)<0) {
				continue;
			}

			struct stat file_stat;
			if (stat(ls[i].d_name, &file_stat) < 0) {
				perror("stat");
				continue;
			}
			if (file_stat.st_size == 0) {
				printf("File size is 0: %s\n", ls[i].d_name);
				continue;
			}

			if (strstr(e, "flac")) {
				play_flac(path, format, flag);
			} else if (strstr(e, "mp3")) {
				play_mp3(path, format, flag);
			} else if (strstr(e, "mp4")) {
				play_aac(path, flag);
			} else if (strstr(e, "m4a")) {
				play_aac(path, flag);
			} else if (strstr(e, "ogg")) {
				play_ogg(path, flag);
			} else if (strstr(e, "wav")) {
				play_wav(path, format, flag);
			} else if (strstr(e, "wma")) {
				play_wma(path, flag);
			} else if (strstr(e, "dsf") || strstr(e, "dff")) {
				play_dsf(path, format, flag);
			} else {
				continue;
			}

			if (cmd=='\\' || cmd=='p' || cmd=='b') {
				i = back;
			} else if (cmd == 'd') {
				// Skip remaining tracks in the current directory
				char cur_dir[PATH_MAX];
				get_dirpart(ls[i].d_name, cur_dir, sizeof(cur_dir));
				while (i + 1 < num) {
					char next_dir[PATH_MAX];
					get_dirpart(ls[i + 1].d_name, next_dir, sizeof(next_dir));
					if (strcmp(cur_dir, next_dir) != 0) break;
					i++;
				}
				back = i - 1;
			}
			if (cmd=='q' || cmd==0x1b) {
				break;
			}
			if (cmd != 'd') back = i-1;
		}
		free(ls);
	} while (loop_mode);
}

#include <sched.h>
void set_realtime_priority()
{
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
		perror("Failed to set real-time priority");
	}
}
void set_cpu(char *c)
{
	char buff[256];
	for (int i=0; i<256; i++) {
		snprintf(buff, 255, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
		FILE *fp = fopen(buff, "w");
		if (!fp) {
			continue;
		}
		fprintf(fp, "%s", c);
		fclose(fp);
	}
}

void list_alsa_devices()
{
	snd_ctl_t *ctl;
	snd_ctl_card_info_t *info;
	snd_ctl_card_info_alloca(&info);
	int card = -1;
	printf("Available ALSA devices:\n");
	while (snd_card_next(&card) >= 0 && card >= 0) {
		char name[32];
		snprintf(name, sizeof(name), "hw:%d", card);
		if (snd_ctl_open(&ctl, name, 0) >= 0) {
			snd_ctl_card_info(ctl, info);
			printf("Card %d: %s\n", card, snd_ctl_card_info_get_name(info));
			snd_ctl_close(ctl);
		}
	}
}

void usage(FILE* fp, int argc, char** argv)
{
	fprintf(fp,
	        "Usage: %s [options] dir\n\n"
	        "Options:\n"
	        "-h                 Print this help message\n"
	        "-d <device name>   Specify ALSA device [e.g., default hw:0,0 plughw:0,0...]\n"
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
	        "  Tab                Pause / resume\n"
	        "  Space              Pause / resume, releasing the ALSA device meanwhile\n"
	        "                     (so other apps can use the sound card)\n"
	        "  Left / Right       Seek -/+ %ds (FLAC, MP3, WAV, OGG)\n"
	        "  Up / Down          Volume +/- %.0f%%\n"
	        "  C                  Toggle crosstalk cancellation\n"
	        "  E                  Toggle real-time PCM<->DSD64(DoP) output\n"
	        "  F                  Cycle playlist format filter (ALL/flac/mp3/m4a/ogg/wav/wma/dsf/dff)\n"
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

	parg_init(&ps);
	while ((c = parg_getopt(&ps, argc, argv, "hd:frxs:t:pclvDTVeo:")) != -1) {
		switch (c) {
		case 1:
			dir = (char*)ps.optarg;
			break;
		case 'd':
			dev = (char*)ps.optarg;
			break;
		case 'f':
			flag |= USE_FLOAT32;
			break;
		case 'r':
			flag |= LS_RECURSIVE;
			break;
		case 'x':
			flag |= LS_RANDOM;
			break;
		case 's':
			regexp = (char*)ps.optarg;
			printf("Search with '%s'.\n", regexp);
			break;
		case 't':
			type = (char*)ps.optarg;
			break;
		case 'p': {
			FILE *fp = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "w");
			fprintf(fp, "tsc");
			fclose(fp);
			set_realtime_priority();
			set_cpu("performance");
			clock = 1;
		}
		break;
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
		case 'o':
			dsd_file_path = (char*)ps.optarg;
			break;
		case 'l':
			loop_mode = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			volume = atof(ps.optarg);
			if (volume < 0.0f || volume > 1.0f) {
				volume = 1.0f;
			}
			break;
		case 'D':
			speaker_distance_m = atof(ps.optarg);
			break;
		case 'h':
			usage(stderr, argc, argv);
			list_alsa_devices();
			return 1;
		}
	}

	apply_alsa_volume(); // push -V's initial value (or the 1.0 default) to the mixer

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
