// ©2017-2025 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>
#include <sys/mman.h>
#include <string.h> // for memcpy
#include <math.h>   // for sin
#include <fcntl.h>  // for open, lseek
#include <unistd.h> // for close

#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
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

#define PARG_IMPLEMENTATION
#include "parg.h"
#include "regexp.h"

#include "random.h"
#include "ls.h"
#include "kbhit.h"

int verbose = 0;
float volume = 1.0f;
int loop_mode = 0;
int cmd;
int key(AUDIO *a)
{
    if (!kbhit()) return 0;

    int c = cmd = getchar();
    if (verbose) printf("%x\n", c);
    if (c==0x20) {
        snd_pcm_pause(a->handle, 1);
        do {
            usleep(1000); // us
        } while (!kbhit());
        getchar(); // clear
        cmd = 0;
        snd_pcm_pause(a->handle, 0);
        snd_pcm_prepare(a->handle);
        return 0;
    }

    return c;
}

//char *dev = "default";  // "plughw:0,0"
char *dev = "hw:0,0";  // BitPerfect

#define USE_FLOAT32   128
#define USE_CROSSTALK 256 // Flag for crosstalk cancellation
#define USE_TEST_MODE 512 // Flag for test mode
#define FRAMES        32
//#define FRAMES        128
#define MAX_DELAY_SAMPLES 16 // Maximum delay samples for crosstalk (e.g., 71µs at 44.1kHz is ~3 samples)

// Crosstalk cancellation parameters
typedef struct {
    int delay_samples;    // Delay in samples (e.g., 3 for 71µs at 44.1kHz)
    float attenuation;    // Attenuation factor (e.g., 0.9)
    float *delay_buffer;  // Buffer to store delayed samples
    int delay_buffer_size; // Size of delay buffer
    int delay_index;      // Current index in delay buffer
} CrosstalkCancel;

void init_crosstalk_cancellation(CrosstalkCancel *xtc, int sample_rate, int channels)
{
    if (channels != 2) return; // Only support stereo
    xtc->delay_samples = (int)(sample_rate * 0.000071); // 71µs delay
    xtc->attenuation = 0.4f; // Natural effect
    xtc->delay_buffer_size = xtc->delay_samples * 2; // Stereo
    if (xtc->delay_buffer_size < 2) xtc->delay_buffer_size = 2; // Ensure at least 2 samples for stereo
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
    if (channels != 2) return; // Only support stereo

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
            data[idx] = fmaxf(fminf(new_left, 1.0f), -1.0f) * volume;  // Apply volume
            data[idx + 1] = fmaxf(fminf(new_right, 1.0f), -1.0f) * volume;
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

            temp[idx] = (temp[idx] - xtc->attenuation * delayed_right) * volume;
            temp[idx + 1] = (temp[idx + 1] - xtc->attenuation * delayed_left) * volume;
        }

        for (int i = 0; i < frames * 2; i++) {
            float val = temp[i] * 32767.0f;
            data[i] = (int16_t)fmaxf(fminf(val, 32767.0f), -32768.0f);
        }
    }
}

void print_test_mode_status(int phase, int flag, CrosstalkCancel *xtc)
{
    printf("\r\e[K");

    if (phase == 0) {
        printf("Phase: Left channel   | ");
    } else if (phase == 1) {
        printf("Phase: Right channel  | ");
    } else {
        printf("Phase: Panning L->R | ");
    }

    if (flag & USE_CROSSTALK) {
        printf("Crosstalk: ON (%.2f) | ", xtc->attenuation);
    } else {
        printf("Crosstalk: OFF        | ");
    }
    
    printf("Keys: [c]toggle-xtalk, [+,-]adjust, [q]uit");
    fflush(stdout);
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
    printf("\e[?25l");
    print_test_mode_status(phase, flag, &xtc);
    while (1) {
        int k = key(&a);
        if (k) {
            if (k == 'q' || k == 0x1b) break;
            if (k == 'c') flag ^= USE_CROSSTALK;
            if (k == '+' || k == '=') {
                xtc.attenuation += 0.05f;
                if (xtc.attenuation > 1.0f) xtc.attenuation = 1.0f;
            }
            if (k == '-') {
                xtc.attenuation -= 0.05f;
                if (xtc.attenuation < 0.0f) xtc.attenuation = 0.0f;
            }
            print_test_mode_status(phase, flag, &xtc);
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
                if (pan > 1.0) pan = 1.0;
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
            print_test_mode_status(phase, flag, &xtc);
        }
    }
    printf("\n\e[?25h");

    AUDIO_close(&a);
    free_crosstalk_cancellation(&xtc);
}

void display_progress(uint64_t current, uint64_t total, int is_time, const char *label)
{
    if (total == 0) return;  // Skip if unknown
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
    if (drwav_init_file(&wav, name, NULL)) {
        printf("%dHz %dch\n", wav.sampleRate, wav.channels);

        AUDIO a;
        if (AUDIO_init(&a, dev, wav.sampleRate, wav.channels, FRAMES, 1, format)) return;

        CrosstalkCancel xtc;
        init_crosstalk_cancellation(&xtc, wav.sampleRate, wav.channels);

        uint64_t (*func)(drwav* pWav, drflac_uint64 framesToRead, void* pBufferOut);
        if (format) {
            func = (uint64_t (*)(drwav *, drflac_uint64, void *))drwav_read_pcm_frames_f32;
            printf(" with FLOAT 32bit\n");
        } else {
            func = (uint64_t (*)(drwav *, drflac_uint64, void *))drwav_read_pcm_frames_s16;
        }

        int c = 0;
        printf("\e[?25l");
        size_t n; // numberOfSamplesActuallyDecoded
        while ((n = func(&wav, a.frames, (drwav_int16*)a.buffer)) > 0) {
            if (n!=a.frames) printf("!");
            if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, wav.channels, format);
            AUDIO_play0(&a);
            AUDIO_wait(&a, 100);
            int k = key(&a);
            if (k=='c') flag ^= USE_CROSSTALK;
            else if (k) break;

            printf("\r%d/%llu", c, wav.totalPCMFrameCount);
            c += n;
        }

        AUDIO_close(&a);
        drwav_uninit(&wav);
        free_crosstalk_cancellation(&xtc);
    }
}

void play_flac(char *name, int format, int flag)
{
    drflac *flac = drflac_open_file(name, NULL);
    if (!flac) {
        fprintf(stderr, "Failed to open FLAC: %s\n", name);
        return;
    }
    printf("%dHz %dbit %dch\n", flac->sampleRate, flac->bitsPerSample, flac->channels);

    AUDIO a;
    if (AUDIO_init(&a, dev, flac->sampleRate, flac->channels, FRAMES, 1, format)) {
        drflac_close(flac);
        return;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, flac->sampleRate, flac->channels);

    uint64_t (*func)(drflac* pFlac, drflac_uint64 framesToRead, void* pBufferOut);
    if (format) {
        func = (uint64_t (*)(drflac *, drflac_uint64, void *))drflac_read_pcm_frames_f32;
        printf(" with FLOAT 32bit\n");
    } else {
        func = (uint64_t (*)(drflac *, drflac_uint64, void *))drflac_read_pcm_frames_s16;
    }

    int c = 0;
    printf("\e[?25l");
    size_t n; // numberOfSamplesActuallyDecoded
    while ((n = func(flac, a.frames, (void*)a.buffer)) > 0) {
        if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, flac->channels, format);
        AUDIO_play0(&a);
        AUDIO_wait(&a, 100);
        int k = key(&a);
        if (k=='c') flag ^= USE_CROSSTALK;
        else if (k) break;

        printf("\r%d/%llu", c, flac->totalPCMFrameCount);
        c += n;
    }
    printf("\e[?25h");

    AUDIO_close(&a);
    drflac_close(flac);
    free_crosstalk_cancellation(&xtc);
}

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

    printf("%dHz %dch\n", decoder->sample_rate_pcm, decoder->channels);
    if (format == SND_PCM_FORMAT_FLOAT_LE) {
        printf(" with FLOAT 32bit\n");
    } else {
        printf(" with S16_LE 16bit\n");
    }

    AUDIO a;
    if (AUDIO_init(&a, dev, decoder->sample_rate_pcm, decoder->channels, FRAMES, 1, format)) {
        dsd_decoder_free(decoder);
        fclose(f);
        return;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, decoder->sample_rate_pcm, decoder->channels);

    uint64_t frames_played = 0;
    printf("\e[?25l");
    size_t n;
    while ((n = dsd_decoder_read_pcm_frames(decoder, a.frames, a.buffer, format)) > 0) {
        if (n != a.frames) printf("!"); // Can happen at the end of the file
        
        if (flag & USE_CROSSTALK) {
            apply_crosstalk_cancellation(&xtc, a.buffer, n, decoder->channels, format);
        }
        
        AUDIO_play0(&a);
        AUDIO_wait(&a, 100);
        
        int k = key(&a);
        if (k == 'c') flag ^= USE_CROSSTALK;
        else if (k) break;

        frames_played += n;
        printf("\r%lu/%lu", frames_played, decoder->totalPCMFrameCount);
        fflush(stdout);
    }
    printf("\n\e[?25h");

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

    printf("%dHz %dch\n", mp3.sampleRate, mp3.channels);
    AUDIO a;
    if (AUDIO_init(&a, dev, mp3.sampleRate, mp3.channels, FRAMES, 1, format)) {
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, mp3.sampleRate, mp3.channels);

    uint64_t (*func)(drmp3*, drflac_uint64, void*);
    if (format) {
        func = (uint64_t (*)(drmp3 *, drflac_uint64, void *))drmp3_read_pcm_frames_f32;
        printf(" with FLOAT 32bit\n");
    } else {
        func = (uint64_t (*)(drmp3 *, drflac_uint64, void *))drmp3_read_pcm_frames_s16;
    }

    int totalPCMFrameCount = drmp3_get_pcm_frame_count(&mp3);
    int c = 0;
    printf("\e[?25l");
    size_t n;
    while ((n = func(&mp3, a.frames, (drmp3_int16*)a.buffer)) > 0) {
        if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, a.buffer, a.frames, mp3.channels, format);
        AUDIO_play0(&a);
        AUDIO_wait(&a, 100);
        int k = key(&a);
        if (k=='c') flag ^= USE_CROSSTALK;
        else if (k) break;

        printf("\r%d/%d", c, totalPCMFrameCount);
        c += n;
    }
    printf("\e[?25h");

    AUDIO_close(&a);
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
    AUDIO a;
    if (AUDIO_init(&a, dev, info.sample_rate, info.channels, FRAMES, 1, 0)) {
        munmap(file_data, len);
        return 1;
    }

    CrosstalkCancel xtc;
    if (flag & USE_CROSSTALK) {
        init_crosstalk_cancellation(&xtc, info.sample_rate, info.channels);
        printf(" with Crosstalk Cancellation\n");
    }

    int c = 0;
    printf("\e[?25l");
    while ((bytes_left >= 0) && (frame_size > 0) && !key(&a)) {
        printf("\r%d", c);

        if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, sample_buf, info.audio_bytes/2/info.channels, info.channels, 0);
        stream_pos += frame_size;
        bytes_left -= frame_size;
        AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
        AUDIO_wait(&a, 100);

        c += frame_size;
        frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
    }
    printf("\e[?25h");

    AUDIO_close(&a);
    mp3_free(mp3);
    munmap(file_data, len);
    if (flag & USE_CROSSTALK) free_crosstalk_cancellation(&xtc);
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
    printf("%dHz %dch\n", v->sample_rate, v->channels);

    AUDIO a;
    if (AUDIO_init(&a, dev, v->sample_rate, v->channels, FRAMES*2, 1, 0)) {
        stb_vorbis_close(v);
        return;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, v->sample_rate, v->channels);

    int c = 0;
    printf("\e[?25l");
    while ((n = stb_vorbis_get_frame_short_interleaved(v, v->channels, outputs, FRAMES*100))) {
        printf("\r%d", c);
        if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, outputs, n, v->channels, 0);
        AUDIO_play(&a, (char*)outputs, n);
        AUDIO_wait(&a, 100);
        int k = key(&a);
        if (k=='c') flag ^= USE_CROSSTALK;
        else if (k) break;
        c += n;
    }
    printf("\e[?25h");

    AUDIO_close(&a);
    stb_vorbis_close(v);
    free_crosstalk_cancellation(&xtc);
}

int play_wma(char *name, int flag)
{
    void *file_data;
    unsigned char *stream_pos;
    short *sample_buf = malloc(MAX_CODED_SUPERFRAME_SIZE * sizeof(short));
    int bytes_left;

    if (!sample_buf) {
        fprintf(stderr, "Error: failed to allocate memory for sample buffer\n");
        return 1;
    }

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        perror("open");
        free(sample_buf);
        return 1;
    }

    int len = lseek(fd, 0, SEEK_END);
    if (len <= 0) {
        fprintf(stderr, "Error: invalid file size\n");
        close(fd);
        free(sample_buf);
        return 1;
    }

    lseek(fd, 0, SEEK_SET);
    file_data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (file_data == MAP_FAILED) {
        perror("mmap");
        free(sample_buf);
        return 1;
    }

    stream_pos = (unsigned char *) file_data;
    bytes_left = len;

    CodecContext cc = {0};
    if (parse_wma_header(stream_pos, bytes_left, &cc) != 0) {
        fprintf(stderr, "Error: failed to parse WMA header\n");
        munmap(file_data, len);
        free(sample_buf);
        return 1;
    }

    if (wma_decode_init_fixed(&cc) < 0) {
        fprintf(stderr, "Error: failed to initialize WMA decoder\n");
        munmap(file_data, len);
        free(sample_buf);
        return 1;
    }

    printf("%dHz %dch\n", cc.sample_rate, cc.channels);
    AUDIO a;
    if (AUDIO_init(&a, dev, cc.sample_rate, cc.channels, FRAMES, 1, 0)) {
        fprintf(stderr, "Error: failed to initialize audio\n");
        wma_decode_end(&cc);
        munmap(file_data, len);
        free(sample_buf);
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, cc.sample_rate, cc.channels);

    printf("\e[?25l");
    while (bytes_left > 0) {
        int out_size = 0;

        int frame_size = wma_decode_superframe(&cc, sample_buf, &out_size, stream_pos, bytes_left);
        if (frame_size <= 0 || out_size <= 0) {
            break;
        }

        if (flag & USE_CROSSTALK) {
            apply_crosstalk_cancellation(&xtc, sample_buf, out_size / (cc.channels * sizeof(short)), cc.channels, 0);
        }

        AUDIO_play(&a, (char*)sample_buf, out_size);
        AUDIO_wait(&a, 100);

        stream_pos += frame_size;
        bytes_left -= frame_size;

        int k = key(&a);
        if (k == 'c') flag ^= USE_CROSSTALK;
        else if (k) break;

        printf("\r%d/%d", len - bytes_left, len);
        fflush(stdout);
    }
    printf("\n\e[?25h");

    AUDIO_close(&a);
    wma_decode_end(&cc);
    munmap(file_data, len);
    free(sample_buf);
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
    info.profile = AAC_PROFILE_LC;  // Consider detecting HE-AAC profile from MP4 'esds' atom if possible

    HAACDecoder aac = AACInitDecoder();
    AACSetRawBlockParams(aac, 0, &info);

    // Check if SBR might be active based on sample rate
    int output_samplerate = info.sampRateCore;
    int sbr_enabled = 0;  // NEW: Track if SBR is assumed
    if (output_samplerate <= 24000) { // Threshold for assuming HE-AAC/SBR
        output_samplerate *= 2;
        sbr_enabled = 1;  // NEW: Explicitly flag SBR (may need to set in decoder if not auto-detected)
    }

    printf("%dHz (output: %dHz) %dch\n", info.sampRateCore, output_samplerate, info.nChans);  // UPDATED: Show both for debugging

    AUDIO a;
    if (AUDIO_init(&a, dev, output_samplerate, info.nChans, FRAMES, 1, 0)) {  // FIX: Use output_samplerate here
        free(file_data);
        close(fd);
        return 1;
    }

    CrosstalkCancel xtc;
    init_crosstalk_cancellation(&xtc, output_samplerate, info.nChans);  // UPDATED: Use output_samplerate for crosstalk init

    printf("\e[?25l");
    while ((bytes_left >= 0)) {
        int r = AACDecode(aac, &stream_pos, &bytes_left, sample_buf);
        printf("\r%d %d", (int)(stream_pos-file_data), bytes_left);
        if (!r) {
            AACGetLastFrameInfo(aac, &info);
            if (flag & USE_CROSSTALK) apply_crosstalk_cancellation(&xtc, sample_buf, AAC_MAX_NSAMPS, info.nChans, 0);
            AUDIO_play(&a, (char*)sample_buf, AAC_MAX_NSAMPS);  // Note: If SBR active, this is 2048 samples/channel
            AUDIO_wait(&a, 100);
        } else {
            int nextSync = AACFindSyncWord(stream_pos, bytes_left);
            printf("\nAAC decode error %d\n", r);
            break;
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
int play_aac(char *name, int flag)
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
            if (!strstr(e, type)) continue;
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
            if (regexec(p, ls[i].d_name, &m, 0)) continue;
        }

        printf("\n\e[1m\e[35m%s\e[0m\n", ls[i].d_name);
        snprintf(path, 1024, "%s", ls[i].d_name);
        if (access(ls[i].d_name, F_OK)<0) continue;

        struct stat file_stat;
        if (stat(ls[i].d_name, &file_stat) < 0) {
            perror("stat");
            continue;
        }
        if (file_stat.st_size == 0) {
            printf("File size is 0: %s\n", ls[i].d_name);
            continue;
        }

        if (strstr(e, "flac")) play_flac(path, format, flag);
        else if (strstr(e, "mp3")) play_mp3(path, format, flag);
        else if (strstr(e, "mp4")) play_aac(path, flag);
        else if (strstr(e, "m4a")) play_aac(path, flag);
        else if (strstr(e, "ogg")) play_ogg(path, flag);
        else if (strstr(e, "wav")) play_wav(path, format, flag);
        else if (strstr(e, "wma")) play_wma(path, flag);
        else if (strstr(e, "dsf")) play_dsf(path, format, flag);
        else continue;

        if (cmd=='\\' || cmd=='p' || cmd=='b') i = back;
        if (cmd=='q' || cmd==0x1b) break;
        back = i-1;
    }
    free(ls);
    } while (loop_mode);
}

void set_cpu(char *c)
{
    char buff[256];
    for (int i=0; i<256; i++) {
        snprintf(buff, 255, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        FILE *fp = fopen(buff, "w");
        if (!fp) continue;
        fprintf(fp, "%s", c);
        fclose(fp);
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
        "-t <ext type>      Specify file type (e.g., flac, mp3, wma, dsf...)\n"
        "-p                 Optimize for Linux platforms\n"
        "-l                 Loop the directory playlist\n"
        "-v                 Verbose mode\n"
        "-V <volume>        Set software volume (0.0-1.0, default 1.0)\n"
        "-c                 Enable crosstalk cancellation\n"
        "-T                 Enable test mode (sine wave: left, right, pan)\n"
        "\n",
        argv[0]);
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
    while ((c = parg_getopt(&ps, argc, argv, "hd:frxs:t:pclvTV")) != -1) {
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
        case 'p':
            {
                FILE *fp = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "w");
                fprintf(fp, "tsc");
                fclose(fp);
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
        case 'l':
            loop_mode = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            volume = atof(ps.optarg);
            if (volume < 0.0f || volume > 1.0f) volume = 1.0f;
            break;
        case 'h':
            usage(stderr, argc, argv);
            return 1;
        }
    }

    if (flag & USE_TEST_MODE) {
        int format = flag & USE_FLOAT32 ? SND_PCM_FORMAT_FLOAT_LE : 0;
        play_test_mode(format, flag);
    } else {
        play_dir(dir, type, regexp, flag);
    }

    if (clock) set_cpu("ondemand");

    return 0;
}
