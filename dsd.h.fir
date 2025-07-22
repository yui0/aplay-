/*
#include <stdio.h>
#include "dsd_decoder.h"

int main() {
    #define DSD_DECODER_IMPLEMENTATION
    #include "dsd_decoder.h"

    // DSFファイルの読み込み（例: ファイルから）
    FILE* file = fopen("sample.dsf", "rb");
    if (!file) {
        printf("Failed to open file\n");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(size);
    fread(data, 1, size, file);
    fclose(file);

    // デコーダ初期化（初期サイズは仮）
    DSDDecoder* decoder = dsd_decoder_init(size, DSD_DEFAULT_SAMPLE_RATE, PCM_DEFAULT_SAMPLE_RATE, 2);
    if (!decoder) {
        free(data);
        return -1;
    }

    // DSFデータロード
    if (dsd_decoder_load_dsf(decoder, data, size) != 0) {
        printf("Failed to load DSF data\n");
        dsd_decoder_free(decoder);
        free(data);
        return -1;
    }

    // PCMに変換
    if (dsd_decoder_convert_to_pcm(decoder) != 0) {
        printf("Failed to convert to PCM\n");
        dsd_decoder_free(decoder);
        free(data);
        return -1;
    }

    // PCMデータ取得
    size_t pcm_size;
    const int32_t* pcm_data = dsd_decoder_get_pcm_data(decoder, &pcm_size);

    // PCMデータ処理（例: ファイルに保存）
    FILE* out = fopen("output.pcm", "wb");
    fwrite(pcm_data, sizeof(int32_t), pcm_size, out);
    fclose(out);

    // 解放
    dsd_decoder_free(decoder);
    free(data);
    return 0;
}
*/

#ifndef DSD_H
#define DSD_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ALSA format definitions
#ifndef SND_PCM_FORMAT_S16_LE
#define SND_PCM_FORMAT_S16_LE 2
#endif
#ifndef SND_PCM_FORMAT_FLOAT_LE
#define SND_PCM_FORMAT_FLOAT_LE 10
#endif

#define DSD_SAMPLES_PER_BYTE 8
#define MAX_CHANNELS 2

typedef struct {
    FILE* file;
    uint64_t dsd_data_offset;
    uint64_t totalPCMFrameCount;
    uint64_t pcm_frames_processed;

    int sample_rate_dsd;
    int sample_rate_pcm;
    int channels;
    uint32_t block_size_bytes;

    uint8_t* block_buffer;
    size_t block_buffer_size;
    size_t current_dsd_bit_index;

} DSDDecoder;

#ifdef DSD_DECODER_IMPLEMENTATION

// Helper functions (no changes)
static uint32_t read_le32(const uint8_t* buf) { return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24); }
static uint64_t read_le64(const uint8_t* buf) { return (uint64_t)buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) | ((uint64_t)buf[3] << 24) | ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 40) | ((uint64_t)buf[6] << 48) | ((uint64_t)buf[7] << 56); }

static int dsd_load_next_block(DSDDecoder* decoder) {
    if (!decoder || !decoder->file) return 0;
    size_t bytes_read = fread(decoder->block_buffer, 1, decoder->block_buffer_size, decoder->file);
    decoder->current_dsd_bit_index = 0;
    return bytes_read > 0;
}

DSDDecoder* dsd_decoder_init_file(FILE* file) {
    if (!file) return NULL;
    DSDDecoder* decoder = (DSDDecoder*)calloc(1, sizeof(DSDDecoder));
    if (!decoder) return NULL;
    decoder->file = file;

    // --- Header Parsing ---
    uint8_t header_buf[80];
    if (fread(header_buf, 1, 28, file) != 28 || strncmp((char*)header_buf, "DSD ", 4) != 0) { free(decoder); return NULL; }
    uint64_t fmt_chunk_offset = 28;
    fseek(file, fmt_chunk_offset, SEEK_SET);
    if (fread(header_buf, 1, 52, file) != 52 || strncmp((char*)header_buf, "fmt ", 4) != 0) { free(decoder); return NULL; }

    uint64_t fmt_chunk_size = read_le64(header_buf + 4);
    decoder->channels = read_le32(header_buf + 24);
    decoder->sample_rate_dsd = read_le32(header_buf + 28);
    uint64_t total_dsd_samples = read_le64(header_buf + 36);
    decoder->block_size_bytes = read_le32(header_buf + 44);

    if (read_le32(header_buf + 32) != 1 || (decoder->channels < 1 || decoder->channels > MAX_CHANNELS) || decoder->block_size_bytes == 0) { free(decoder); return NULL; }
    
    if (decoder->sample_rate_dsd == 2822400) decoder->sample_rate_pcm = 44100;
    else if (decoder->sample_rate_dsd == 5644800) decoder->sample_rate_pcm = 88200;
    else if (decoder->sample_rate_dsd == 11289600) decoder->sample_rate_pcm = 176400;
    else decoder->sample_rate_pcm = decoder->sample_rate_dsd / 64;

    size_t decimation_factor = decoder->sample_rate_dsd / decoder->sample_rate_pcm;
    decoder->totalPCMFrameCount = total_dsd_samples / decimation_factor;

    decoder->block_buffer_size = decoder->block_size_bytes * decoder->channels;
    decoder->block_buffer = (uint8_t*)malloc(decoder->block_buffer_size);
    if (!decoder->block_buffer) { free(decoder); return NULL; }

    fseek(file, fmt_chunk_offset + fmt_chunk_size, SEEK_SET);
    char chunk_id[12];
    if (fread(chunk_id, 1, 12, file) != 12 || strncmp(chunk_id, "data", 4) != 0) { free(decoder->block_buffer); free(decoder); return NULL; }
    fseek(file, ftell(file) - 12 + 12, SEEK_SET);
    
    dsd_load_next_block(decoder);

    return decoder;
}

void dsd_decoder_free(DSDDecoder* decoder) {
    if (decoder) {
        free(decoder->block_buffer);
        free(decoder);
    }
}

size_t dsd_decoder_read_pcm_frames(DSDDecoder* decoder, size_t frames_to_read, void* buffer, int format) {
    if (!decoder || !buffer || frames_to_read == 0 || decoder->pcm_frames_processed >= decoder->totalPCMFrameCount) return 0;

    size_t decimation_factor = decoder->sample_rate_dsd / decoder->sample_rate_pcm;
    if (decimation_factor == 0) return 0;

    size_t frames_read = 0;
    int16_t* buffer_s16 = (int16_t*)buffer;
    float* buffer_f32 = (float*)buffer;
    size_t block_size_bits = decoder->block_size_bytes * DSD_SAMPLES_PER_BYTE;

    for (size_t i = 0; i < frames_to_read; ++i) {
        for (int ch = 0; ch < decoder->channels; ++ch) {
            const uint8_t* dsd_channel_data = decoder->block_buffer + (ch * decoder->block_size_bytes);
            
            // --- Triangular Windowed FIR Filter ---
            double weighted_sum = 0.0;
            double total_weight = 0.0;
            double half_R = (double)decimation_factor / 2.0;

            for (size_t k = 0; k < decimation_factor; ++k) {
                size_t current_bit = decoder->current_dsd_bit_index + k;
                
                size_t byte_idx = current_bit / DSD_SAMPLES_PER_BYTE;
                int bit_pos = 7 - (current_bit % DSD_SAMPLES_PER_BYTE);
                
                double dsd_val = ((dsd_channel_data[byte_idx] >> bit_pos) & 1) ? 1.0 : -1.0;

                // Apply a triangular window weight to the DSD sample
                // This gives more importance to samples in the middle of the window
                double weight = 1.0 - fabs(((double)k + 0.5) - half_R) / half_R;
                
                weighted_sum += dsd_val * weight;
                total_weight += weight;
            }
            
            // Normalize the weighted sum
            double pcm_val = (total_weight > 0) ? (weighted_sum / total_weight) : 0.0;

            // Apply some gain, as FIR filter output can have lower amplitude
            pcm_val *= 2.0;

            // Write to output buffer with clipping
            if (format == SND_PCM_FORMAT_FLOAT_LE) {
                if (pcm_val > 1.0) pcm_val = 1.0; if (pcm_val < -1.0) pcm_val = -1.0;
                buffer_f32[i * decoder->channels + ch] = (float)pcm_val;
            } else { // SND_PCM_FORMAT_S16_LE
                int32_t s16_val = (int32_t)(pcm_val * 32767.0);
                if (s16_val > 32767) s16_val = 32767; if (s16_val < -32768) s16_val = -32768;
                buffer_s16[i * decoder->channels + ch] = (int16_t)s16_val;
            }
        }
        
        decoder->current_dsd_bit_index += decimation_factor;

        if (decoder->current_dsd_bit_index >= block_size_bits) {
            if (!dsd_load_next_block(decoder)) {
                frames_read++;
                goto end_loop;
            }
        }
        
        decoder->pcm_frames_processed++;
        frames_read++;
        if (decoder->pcm_frames_processed >= decoder->totalPCMFrameCount) goto end_loop;
    }

end_loop:
    return frames_read;
}

#endif // DSD_DECODER_IMPLEMENTATION
#endif // DSD_H
