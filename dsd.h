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

#define DSD_SAMPLES_PER_BYTE 8
#define DSD_DEFAULT_SAMPLE_RATE 2822400 // DSD64: 2.8224 MHz
#define PCM_DEFAULT_SAMPLE_RATE 44100   // 44.1 kHz PCM output
#define DSD_FRAMES 32

typedef struct {
    char chunk_id[4];       // "DSD "
    uint64_t chunk_size;    // Size of DSD chunk
    uint64_t file_size;     // Total file size
    uint64_t metadata_offset; // Offset to metadata (0 if none)
} DSFHeader;

typedef struct {
    char chunk_id[4];       // "fmt "
    uint64_t chunk_size;    // Size of fmt chunk
    uint32_t format_version; // Format version (usually 1)
    uint32_t format_id;     // 0: DSD raw
    uint32_t channel_type;  // 1: mono, 2: stereo, etc.
    uint32_t channel_num;   // Number of channels
    uint32_t sample_rate;   // Sampling frequency (e.g., 2822400 Hz)
    uint32_t bits_per_sample; // Bits per sample (1 for DSD)
    uint64_t sample_count;  // Total samples per channel
    uint32_t block_size;    // Block size per channel (usually 4096)
} DSFFmtChunk;

typedef struct {
    uint8_t* dsd_data;      // DSD data buffer
    size_t dsd_data_size;   // Size of DSD data in bytes
    int32_t* pcm_buffer;    // Temporary PCM buffer for streaming
    size_t pcm_buffer_size; // Size of PCM buffer in samples
    size_t dsd_data_index;  // Current index in DSD data for streaming
    uint64_t totalPCMFrameCount; // Total PCM frames
    int sample_rate_dsd;    // DSD sample rate
    int sample_rate_pcm;    // PCM sample rate
    int channels;           // Number of channels (1: mono, 2: stereo)
    uint32_t data_offset;   // Offset to DSD data in DSF file
} DSDDecoder;

#ifdef DSD_DECODER_IMPLEMENTATION

static uint32_t read_le32(const uint8_t* buf) {
    return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

static uint64_t read_le64(const uint8_t* buf) {
    return ((uint64_t)buf[7] << 56) | ((uint64_t)buf[6] << 48) |
           ((uint64_t)buf[5] << 40) | ((uint64_t)buf[4] << 32) |
           ((uint64_t)buf[3] << 24) | ((uint64_t)buf[2] << 16) |
           ((uint64_t)buf[1] << 8) | buf[0];
}

DSDDecoder* dsd_decoder_init(size_t dsd_data_size, int sample_rate_dsd, int sample_rate_pcm, int channels) {
    if (channels != 1 && channels != 2) return NULL; // Support only mono or stereo
    DSDDecoder* decoder = (DSDDecoder*)malloc(sizeof(DSDDecoder));
    if (!decoder) return NULL;

    decoder->dsd_data = NULL;
    decoder->dsd_data_size = dsd_data_size;
    decoder->sample_rate_dsd = sample_rate_dsd;
    decoder->sample_rate_pcm = sample_rate_pcm;
    decoder->channels = channels;
    decoder->data_offset = 0;
    decoder->dsd_data_index = 0;

    // Calculate total PCM frames
    size_t dsd_samples = dsd_data_size * DSD_SAMPLES_PER_BYTE;
    decoder->totalPCMFrameCount = (size_t)((double)dsd_samples / (sample_rate_dsd / sample_rate_pcm));
    decoder->pcm_buffer_size = DSD_FRAMES * channels;
    decoder->pcm_buffer = (int32_t*)calloc(decoder->pcm_buffer_size, sizeof(int32_t));
    if (!decoder->pcm_buffer) {
        free(decoder);
        return NULL;
    }

    return decoder;
}

void dsd_decoder_free(DSDDecoder* decoder) {
    if (decoder) {
        free(decoder->dsd_data);
        free(decoder->pcm_buffer);
        free(decoder);
    }
}

int dsd_decoder_load_dsf(DSDDecoder* decoder, const uint8_t* data, size_t size) {
    if (!decoder || !data || size < 28) return -1;

    DSFHeader header;
    memcpy(header.chunk_id, data, 4);
    if (strncmp(header.chunk_id, "DSD ", 4) != 0) return -1;

    header.chunk_size = read_le64(data + 4);
    header.file_size = read_le64(data + 12);
    header.metadata_offset = read_le64(data + 20);

    if (header.file_size > size) return -1;

    DSFFmtChunk fmt;
    memcpy(fmt.chunk_id, data + 28, 4);
    if (strncmp(fmt.chunk_id, "fmt ", 4) != 0) return -1;

    fmt.chunk_size = read_le64(data + 32);
    fmt.format_version = read_le32(data + 40);
    fmt.format_id = read_le32(data + 44);
    fmt.channel_type = read_le32(data + 48);
    fmt.channel_num = read_le32(data + 52);
    fmt.sample_rate = read_le32(data + 56);
    fmt.bits_per_sample = read_le32(data + 60);
    fmt.sample_count = read_le64(data + 64);
    fmt.block_size = read_le32(data + 72);

    if (fmt.bits_per_sample != 1 || fmt.format_id != 0) return -1;
    if (fmt.channel_num != 1 && fmt.channel_num != 2) return -1; // Support only mono or stereo

    decoder->channels = fmt.channel_num;
    decoder->sample_rate_dsd = fmt.sample_rate;
    decoder->dsd_data_size = fmt.sample_count / DSD_SAMPLES_PER_BYTE;
    decoder->totalPCMFrameCount = (size_t)((double)fmt.sample_count / (fmt.sample_rate / decoder->sample_rate_pcm));

    size_t data_offset = 28 + fmt.chunk_size + 12;
    if (data_offset + decoder->dsd_data_size > size) return -1;

    decoder->dsd_data = (uint8_t*)malloc(decoder->dsd_data_size);
    if (!decoder->dsd_data) return -1;

    memcpy(decoder->dsd_data, data + data_offset, decoder->dsd_data_size);
    decoder->data_offset = data_offset;

    return 0;
}

int dsd_decoder_convert_to_pcm(DSDDecoder* decoder) {
    if (!decoder || !decoder->dsd_data || decoder->channels == 0) return -1;

    // DSDサンプルレートとPCMサンプルレートの比率（間引き率）
    size_t decimation_factor = decoder->sample_rate_dsd / decoder->sample_rate_pcm;
    if (decimation_factor == 0) return -1;

    // チャンネルごとのDSDデータサイズ（バイト単位）
    size_t dsd_channel_size_bytes = decoder->dsd_data_size / decoder->channels;
    // チャンネルごとのDSDサンプル数
    size_t dsd_channel_samples = dsd_channel_size_bytes * DSD_SAMPLES_PER_BYTE;
    // チャンネルごとのPCMサンプル数
    size_t pcm_channel_samples = decoder->totalPCMFrameCount;

    // チャンネルごとに処理を行う
    for (int ch = 0; ch < decoder->channels; ++ch) {
        // 現在のチャンネルのDSDデータの開始ポインタ
        const uint8_t* dsd_channel_data = decoder->dsd_data + (ch * dsd_channel_size_bytes);
        size_t dsd_sample_idx = 0;

        // このチャンネルのPCMサンプルを生成するループ
        for (size_t pcm_s_idx = 0; pcm_s_idx < pcm_channel_samples; ++pcm_s_idx) {
            int32_t sum = 0;

            // 1つのPCMサンプルを生成するために、decimation_factor分のDSDサンプルを処理する
            for (size_t i = 0; i < decimation_factor; ++i) {
                if (dsd_sample_idx >= dsd_channel_samples) break;

                // dsd_sample_idxからバイト位置とビット位置を計算
                size_t byte_idx = dsd_sample_idx / DSD_SAMPLES_PER_BYTE;
                int bit_pos = 7 - (dsd_sample_idx % DSD_SAMPLES_PER_BYTE);

                // DSDビットを読み込み、+1か-1に変換
                int sample = (dsd_channel_data[byte_idx] & (1 << bit_pos)) ? 1 : -1;
                sum += sample;

                dsd_sample_idx++;
            }

            // 平均化してスケーリングを行い、PCMデータとして格納する
            // PCMデータはインターリーブ形式で格納 (L, R, L, R, ...)
            int32_t pcm_sample = (int32_t)((double)sum / decimation_factor * INT32_MAX);
            decoder->pcm_buffer[pcm_s_idx * decoder->channels + ch] = pcm_sample;
        }
    }

    return 0;
}

size_t dsd_decoder_read_pcm_frames(DSDDecoder* decoder, size_t frames_to_read, void* buffer, int format) {
    if (!decoder || !decoder->dsd_data || !buffer) return 0;

    size_t decimation_factor = decoder->sample_rate_dsd / decoder->sample_rate_pcm;
    if (decimation_factor == 0) return 0;

    size_t dsd_channel_size_bytes = decoder->dsd_data_size / decoder->channels;
    size_t dsd_channel_samples = dsd_channel_size_bytes * DSD_SAMPLES_PER_BYTE;
    size_t pcm_channel_samples = decoder->totalPCMFrameCount;

    size_t pcm_frames_read = 0;
    size_t dsd_samples_needed = frames_to_read * decimation_factor;
    size_t dsd_bytes_needed = (dsd_samples_needed + DSD_SAMPLES_PER_BYTE - 1) / DSD_SAMPLES_PER_BYTE;

    if (decoder->dsd_data_index >= dsd_channel_samples) return 0;

    size_t frames_available = pcm_channel_samples - (decoder->dsd_data_index / decimation_factor);
    if (frames_to_read > frames_available) frames_to_read = frames_available;

    if (frames_to_read == 0) return 0;

    // Temporary DSD buffer for the current chunk
    size_t chunk_dsd_size = dsd_bytes_needed * decoder->channels;
    if (chunk_dsd_size > decoder->dsd_data_size - (decoder->dsd_data_index / DSD_SAMPLES_PER_BYTE) * decoder->channels) {
        chunk_dsd_size = decoder->dsd_data_size - (decoder->dsd_data_index / DSD_SAMPLES_PER_BYTE) * decoder->channels;
    }

    // Temporary PCM buffer
    size_t pcm_samples = frames_to_read * decoder->channels;
    if (pcm_samples > decoder->pcm_buffer_size) {
        free(decoder->pcm_buffer);
        decoder->pcm_buffer_size = pcm_samples;
        decoder->pcm_buffer = (int32_t*)calloc(decoder->pcm_buffer_size, sizeof(int32_t));
        if (!decoder->pcm_buffer) return 0;
    }

    // Process each channel
    for (int ch = 0; ch < decoder->channels; ++ch) {
        const uint8_t* dsd_channel_data = decoder->dsd_data + (ch * dsd_channel_size_bytes) + (decoder->dsd_data_index / DSD_SAMPLES_PER_BYTE);
        size_t dsd_sample_idx = decoder->dsd_data_index % DSD_SAMPLES_PER_BYTE;

        for (size_t pcm_s_idx = 0; pcm_s_idx < frames_to_read; ++pcm_s_idx) {
            int32_t sum = 0;

            for (size_t i = 0; i < decimation_factor; ++i) {
                size_t total_dsd_idx = decoder->dsd_data_index + i;
                if (total_dsd_idx >= dsd_channel_samples) break;

                size_t byte_idx = total_dsd_idx / DSD_SAMPLES_PER_BYTE;
                int bit_pos = 7 - (total_dsd_idx % DSD_SAMPLES_PER_BYTE);

                int sample = (dsd_channel_data[byte_idx] & (1 << bit_pos)) ? 1 : -1;
                sum += sample;
            }

            int32_t pcm_sample = (int32_t)((double)sum / decimation_factor * INT32_MAX);
            decoder->pcm_buffer[pcm_s_idx * decoder->channels + ch] = pcm_sample;
        }
    }

    // Convert PCM to output format
    if (format == SND_PCM_FORMAT_FLOAT_LE) {
        float *buffer_f32 = (float*)buffer;
        for (size_t i = 0; i < pcm_samples; i++) {
            buffer_f32[i] = decoder->pcm_buffer[i] / (float)INT32_MAX;
        }
    } else {
        int16_t *buffer_s16 = (int16_t*)buffer;
        for (size_t i = 0; i < pcm_samples; i++) {
            float val = decoder->pcm_buffer[i] / (float)INT32_MAX * 32767.0f;
            if (val > 32767.0f) val = 32767.0f;
            if (val < -32768.0f) val = -32768.0f;
            buffer_s16[i] = (int16_t)val;
        }
    }

    decoder->dsd_data_index += dsd_samples_needed;
    pcm_frames_read = frames_to_read;

    return pcm_frames_read;
}

#endif // DSD_DECODER_IMPLEMENTATION
#endif // DSD_H
