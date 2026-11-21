/* Streaming MP3 decoder adapted from SHIELD DJ Lite's minimp3/NDSP path.
   DJ effects and speed changes are intentionally absent. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "audio.h"
#ifdef SHIELD_HOST_TEST
#include "audio_host_stub.h"
#else
#include <3ds.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define INPUT_BYTES 16384
#define BLOCK_FRAMES 4096
#define BLOCK_COUNT 4
#define MAX_BAD_BYTES 8192
#define CHANNEL 0

typedef struct { ndspWaveBuf wave; int16_t *pcm; } Block;
static Block blocks[BLOCK_COUNT];
static FILE *file;
static mp3dec_t decoder;
static uint8_t input[INPUT_BYTES];
static int input_size;
static int16_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t pending[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static int pending_frames, pending_at;
static bool ready, active, paused, eof, decoded_any;
static unsigned sample_rate;
static uint32_t duration_ms;
static uint64_t payload_bytes;
static uint64_t played_ms, last_tick;
static const char *error_text = "";

static void set_error(const char *message) { error_text = message; }

static bool queue(Block *block) {
    if (!block->pcm || !file || eof) return false;
    memset(&block->wave, 0, sizeof(block->wave));
    int frames = 0, bad_bytes = 0;
    while (frames < BLOCK_FRAMES) {
        if (pending_at < pending_frames) {
            int count = pending_frames - pending_at;
            if (count > BLOCK_FRAMES - frames) count = BLOCK_FRAMES - frames;
            memcpy(block->pcm + 2*frames, pending + 2*pending_at,
                   (size_t)count * 4);
            pending_at += count;
            frames += count;
            continue;
        }
        pending_frames = pending_at = 0;
        if (input_size < 4096 && !feof(file)) {
            size_t got = fread(input + input_size, 1, INPUT_BYTES - input_size, file);
            input_size += (int)got;
            if (!got && ferror(file)) {
                set_error("Erreur de lecture de la carte SD.");
                eof = true;
                break;
            }
        }
        if (input_size < 4) { eof = true; break; }
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&decoder, input, input_size, decoded, &info);
        if (info.frame_bytes <= 0 || info.frame_bytes > input_size) {
            memmove(input, input + 1, --input_size);
            if (++bad_bytes > MAX_BAD_BYTES) {
                set_error("MP3 illisible ou corrompu."); eof = true; break;
            }
            continue;
        }
        memmove(input, input + info.frame_bytes, input_size - info.frame_bytes);
        input_size -= info.frame_bytes;
        if (samples <= 0 || (info.channels != 1 && info.channels != 2) ||
            info.hz < 8000 || info.hz > 48000) {
            bad_bytes += info.frame_bytes;
            if (bad_bytes > MAX_BAD_BYTES) {
                set_error("Format MP3 non pris en charge."); eof = true; break;
            }
            continue;
        }
        if (sample_rate && sample_rate != (unsigned)info.hz) {
            set_error("Frequence MP3 variable non prise en charge.");
            eof = true;
            break;
        }
        bad_bytes = 0;
        decoded_any = true;
        if (!sample_rate) {
            sample_rate = (unsigned)info.hz;
            ndspChnSetRate(CHANNEL, (float)sample_rate);
            if (!duration_ms && payload_bytes && info.bitrate_kbps > 0)
                duration_ms = payload_bytes * 8 / (unsigned)info.bitrate_kbps;
        }
        for (int i = 0; i < samples; ++i) {
            int16_t left = decoded[i * info.channels];
            pending[2*i] = left;
            pending[2*i+1] = info.channels == 1 ? left : decoded[2*i+1];
        }
        pending_frames = samples;
    }
    if (!frames) return false;
    DSP_FlushDataCache(block->pcm, (size_t)frames * 4);
    block->wave.data_vaddr = block->pcm;
    block->wave.nsamples = frames;
    ndspChnWaveBufAdd(CHANNEL, &block->wave);
    return true;
}

bool audio_init(void) {
    if (R_FAILED(ndspInit())) {
        set_error("Audio indisponible : verifier dspfirm.cdc.");
        return false;
    }
    ready = true;
    for (int i = 0; i < BLOCK_COUNT; ++i) {
        blocks[i].pcm = linearAlloc(BLOCK_FRAMES * 4);
        if (!blocks[i].pcm) {
            set_error("Memoire audio insuffisante.");
            audio_exit();
            return false;
        }
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CHANNEL);
    ndspChnSetInterp(CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0}; mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(CHANNEL, mix);
    set_error("");
    return true;
}

void audio_stop(void) {
    if (ready) {
        ndspChnWaveBufClear(CHANNEL);
        ndspChnSetPaused(CHANNEL, false);
    }
    if (file) fclose(file);
    file = NULL;
    active = paused = eof = false;
    input_size = pending_frames = pending_at = 0;
    played_ms = last_tick = 0;
    aptSetSleepAllowed(true);
}

void audio_exit(void) {
    audio_stop();
    for (int i = 0; i < BLOCK_COUNT; ++i) {
        if (blocks[i].pcm) linearFree(blocks[i].pcm);
        blocks[i].pcm = NULL;
    }
    if (ready) ndspExit();
    ready = false;
}

bool audio_open(const char *path, uint32_t known_duration_ms) {
    audio_stop();
    set_error("");
    if (!ready) { set_error("Moteur audio indisponible."); return false; }
    file = fopen(path, "rb");
    if (!file) { set_error("MP3 absent sur la carte SD."); return false; }
    uint8_t tag[10];
    if (fread(tag, 1, 10, file) == 10 && !memcmp(tag, "ID3", 3)) {
        unsigned bytes = ((unsigned)(tag[6]&127)<<21) | ((unsigned)(tag[7]&127)<<14) |
                         ((unsigned)(tag[8]&127)<<7) | (unsigned)(tag[9]&127);
        unsigned offset = 10 + bytes + ((tag[5]&16) ? 10 : 0);
        if (offset > LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0) {
            set_error("En-tete MP3 invalide."); audio_stop(); return false;
        }
    } else rewind(file);
    duration_ms = known_duration_ms;
    long at=ftell(file);
    payload_bytes=0;
    if (at >= 0 && fseek(file,0,SEEK_END) == 0) {
        long end=ftell(file);
        if (end > at) payload_bytes=(uint64_t)(end-at);
        fseek(file,at,SEEK_SET);
    }
    sample_rate = 0;
    decoded_any = false;
    mp3dec_init(&decoder);
    ndspChnWaveBufClear(CHANNEL);
    ndspChnSetPaused(CHANNEL, true);
    int queued = 0;
    for (int i = 0; i < BLOCK_COUNT && !eof; ++i) queued += queue(&blocks[i]);
    if (!queued || !decoded_any) {
        if (!*error_text) set_error("Ce fichier ne contient pas d'audio MP3 lisible.");
        audio_stop(); return false;
    }
    active = true;
    last_tick = osGetTime();
    aptSetSleepAllowed(false);
    ndspChnSetPaused(CHANNEL, false);
    return true;
}

void audio_toggle_pause(void) {
    if (!active) return;
    uint64_t now = osGetTime();
    if (!paused && now > last_tick) played_ms += now - last_tick;
    paused = !paused;
    last_tick = now;
    ndspChnSetPaused(CHANNEL, paused);
}

void audio_update(void) {
    if (!active || !ready) return;
    uint64_t now = osGetTime();
    if (!paused && now > last_tick) played_ms += now - last_tick;
    last_tick = now;
    if (paused) return;
    for (int i = 0; i < BLOCK_COUNT && !eof; ++i)
        if (blocks[i].wave.status == NDSP_WBUF_DONE) queue(&blocks[i]);
    bool pending_wave=false;
    for (int i=0;i<BLOCK_COUNT;++i)
        if (blocks[i].wave.status == NDSP_WBUF_QUEUED ||
            blocks[i].wave.status == NDSP_WBUF_PLAYING) pending_wave=true;
    if (eof && !pending_wave) {
        audio_stop();
    }
}

bool audio_playing(void) { return active; }
bool audio_paused(void) { return paused; }
uint32_t audio_position_ms(void) {
    uint64_t value = played_ms;
    if (active && !paused && osGetTime() > last_tick) value += osGetTime() - last_tick;
    if (duration_ms && value > duration_ms) value = duration_ms;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}
uint32_t audio_duration_ms(void) { return duration_ms; }
const char *audio_error(void) { return error_text; }
