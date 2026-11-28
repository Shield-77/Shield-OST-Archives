/* Streaming MP3 decoder adapted from SHIELD DJ Lite's minimp3/NDSP path.
   DJ effects and speed changes are intentionally absent. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#include "minimp4.h"
#include "neaacdec.h"
#include "audio.h"
#ifdef SHIELD_HOST_TEST
#include "audio_host_stub.h"
#else
#include <3ds.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#define INPUT_BYTES 16384
#define BLOCK_FRAMES 4096
#define BLOCK_COUNT 4
#define MAX_BAD_BYTES 8192
#define CHANNEL 0

typedef struct { ndspWaveBuf wave; int16_t *pcm; } Block;
static Block blocks[BLOCK_COUNT];
static FILE *file;
static drwav *wav_file;
static drwav wav_storage;
static drflac *flac_file;
static int mode;
static NeAACDecHandle aac_decoder;
static MP4D_demux_t mp4;
static bool mp4_open;
static unsigned mp4_track, mp4_sample;
static int16_t lossless_samples[BLOCK_FRAMES*2];
static mp3dec_t decoder;
static uint8_t input[INPUT_BYTES];
static int input_size;
static int16_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t pending[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static int pending_frames, pending_at;
static bool ready, active, paused, eof, decoded_any;
static bool streaming_input, stream_buffering;
static uint64_t stream_retry_after;
static unsigned sample_rate;
static uint32_t duration_ms;
static uint64_t payload_bytes;
static uint64_t played_frames;
static unsigned initial_peak, initial_frames, consumed_blocks;
static const char *error_text = "";

static void set_error(const char *message) { error_text = message; }

static int mp4_read(int64_t offset, void *buffer, size_t size, void *token) {
    FILE *input_file=token;
    if (offset<0 || offset>LONG_MAX || size>INPUT_BYTES ||
        fseek(input_file,(long)offset,SEEK_SET)!=0) return 1;
    return fread(buffer,1,size,input_file)==size ? 0 : 1;
}

static bool aac_fill(void) {
    if (mode==4) {
        if (mp4_sample>=mp4.track[mp4_track].sample_count) { eof=true; return false; }
        unsigned bytes=0;
        MP4D_file_offset_t offset=MP4D_frame_offset(&mp4,mp4_track,mp4_sample++, &bytes,NULL,NULL);
        if (!offset || !bytes || bytes>INPUT_BYTES || mp4_read(offset,input,bytes,file)) {
            set_error("Echantillon M4A invalide."); eof=true; return false;
        }
        input_size=(int)bytes; return true;
    }
    if (input_size<4096 && (streaming_input || !feof(file))) {
        if (streaming_input) clearerr(file);
        size_t got=fread(input+input_size,1,INPUT_BYTES-input_size,file);
        input_size+=(int)got;
        if (!got && ferror(file)) { set_error("Erreur de lecture SD."); eof=true; return false; }
        if (!got && streaming_input) return false;
    }
    if (input_size<7) { if (!streaming_input) eof=true; return false; }
    return true;
}

static bool queue(Block *block) {
    if (!block->pcm || (!file && !wav_file && !flac_file) || eof) return false;
    memset(&block->wave, 0, sizeof(block->wave));
    int frames = 0, bad_bytes = 0;
    if (mode==1 || mode==2) {
        unsigned channels=wav_file ? wav_file->channels : flac_file->channels;
        uint64_t got=wav_file ? drwav_read_pcm_frames_s16(wav_file,BLOCK_FRAMES,lossless_samples) :
            drflac_read_pcm_frames_s16(flac_file,BLOCK_FRAMES,lossless_samples);
        if (got>BLOCK_FRAMES) got=BLOCK_FRAMES;
        frames=(int)got;
        for (int i=0;i<frames;++i) {
            block->pcm[2*i]=lossless_samples[i*channels];
            block->pcm[2*i+1]=channels==1 ? lossless_samples[i] : lossless_samples[2*i+1];
        }
        if (got<BLOCK_FRAMES) eof=true;
    } else if (mode==3 || mode==4) {
        int bad=0;
        while (frames<BLOCK_FRAMES) {
            if (pending_at<pending_frames) {
                int count=pending_frames-pending_at;
                if (count>BLOCK_FRAMES-frames) count=BLOCK_FRAMES-frames;
                memcpy(block->pcm+2*frames,pending+2*pending_at,(size_t)count*4);
                pending_at+=count; frames+=count; continue;
            }
            pending_at=pending_frames=0;
            if (!aac_fill()) break;
            NeAACDecFrameInfo info={0};
            int16_t *pcm=NeAACDecDecode(aac_decoder,&info,input,(unsigned long)input_size);
            if (mode==3) {
                unsigned consumed=info.bytesconsumed ? info.bytesconsumed : 1;
                if (consumed>(unsigned)input_size) consumed=(unsigned)input_size;
                memmove(input,input+consumed,(size_t)input_size-consumed);
                input_size-=(int)consumed;
            } else input_size=0;
            if (info.error || !pcm || !info.samples || info.channels<1 || info.channels>2 ||
                info.samples/info.channels>MINIMP3_MAX_SAMPLES_PER_FRAME ||
                info.samplerate<8000 || info.samplerate>48000) {
                if (++bad>12) { set_error("AAC incompatible ou corrompu."); eof=true; break; }
                continue;
            }
            if (sample_rate!=(unsigned)info.samplerate) {
                if (decoded_any) { set_error("Frequence AAC variable non prise en charge."); eof=true; break; }
                sample_rate=(unsigned)info.samplerate;
                ndspChnSetRate(CHANNEL,(float)sample_rate);
            }
            decoded_any=true; bad=0;
            int count=(int)(info.samples/info.channels);
            for (int i=0;i<count;++i) {
                pending[2*i]=pcm[i*info.channels];
                pending[2*i+1]=info.channels==1 ? pcm[i] : pcm[2*i+1];
            }
            pending_frames=count;
        }
    } else {
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
        if (input_size < 4096 && (streaming_input || !feof(file))) {
            if (streaming_input) clearerr(file);
            size_t got = fread(input + input_size, 1, INPUT_BYTES - input_size, file);
            input_size += (int)got;
            if (!got && ferror(file)) {
                set_error("Erreur de lecture de la carte SD.");
                eof = true;
                break;
            }
            if (!got && streaming_input) break;
        }
        if (input_size < 4) { if (!streaming_input) eof = true; break; }
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
    }
    if (!frames) return false;
    DSP_FlushDataCache(block->pcm, (size_t)frames * 4);
    block->wave.data_vaddr = block->pcm;
    block->wave.nsamples = frames;
    ndspChnWaveBufAdd(CHANNEL, &block->wave);
    return true;
}

static int queue_initial(int index) {
    Block *block=&blocks[index];
    if (!queue(block)) return 0;
    initial_frames+=(unsigned)block->wave.nsamples;
    for (size_t i=0;i<(size_t)block->wave.nsamples*2;++i) {
        int sample=block->pcm[i];
        unsigned magnitude=(unsigned)(sample<0 ? -sample : sample);
        if (magnitude>initial_peak) initial_peak=magnitude;
    }
    return 1;
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
    ndspSetMasterVol(1.0f);
    ndspSetClippingMode(NDSP_CLIP_SOFT);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CHANNEL);
    ndspChnSetInterp(CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0}; mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(CHANNEL, mix);
    ndspChnSetPaused(CHANNEL, false);
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
    if (wav_file) drwav_uninit(wav_file);
    if (flac_file) drflac_close(flac_file);
    wav_file=NULL; flac_file=NULL; mode=0;
    if (aac_decoder) NeAACDecClose(aac_decoder);
    aac_decoder=NULL;
    if (mp4_open) MP4D_close(&mp4);
    mp4_open=false; mp4_sample=0;
    active = paused = eof = false;
    streaming_input=stream_buffering=false;
    stream_retry_after=0;
    decoded_any=false;
    sample_rate=duration_ms=0;
    payload_bytes=0;
    input_size = pending_frames = pending_at = 0;
    played_frames = 0;
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

static bool audio_open_impl(const char *path, uint32_t known_duration_ms,bool stream) {
    audio_stop();
    streaming_input=stream;
    initial_peak=initial_frames=consumed_blocks=0;
    set_error("");
    if (!ready) { set_error("Moteur audio indisponible."); return false; }
    const char *extension=strrchr(path,'.');
    if (extension && !strcasecmp(extension,".wav")) mode=1;
    else if (extension && !strcasecmp(extension,".flac")) mode=2;
    else if (extension && !strcasecmp(extension,".aac")) mode=3;
    else if (extension && !strcasecmp(extension,".m4a")) mode=4;
    else if (extension && !strcasecmp(extension,".mp3")) mode=0;
    else { set_error("Format audio non pris en charge."); return false; }
    if (mode) {
        if (mode==3 || mode==4) goto open_aac;
        if (mode==1 && drwav_init_file(&wav_storage,path,NULL)) wav_file=&wav_storage;
        else flac_file=drflac_open_file(path,NULL);
        if (!wav_file && !flac_file) { set_error("Audio illisible ou corrompu."); audio_stop(); return false; }
        unsigned channels=wav_file ? wav_file->channels : flac_file->channels;
        sample_rate=wav_file ? wav_file->sampleRate : flac_file->sampleRate;
        uint64_t count=wav_file ? wav_file->totalPCMFrameCount : flac_file->totalPCMFrameCount;
        if (channels<1 || channels>2 || sample_rate<8000 || sample_rate>48000) {
            set_error("Canaux ou frequence audio non pris en charge."); audio_stop(); return false;
        }
        duration_ms=known_duration_ms ? known_duration_ms :
            count && count<UINT32_MAX ? (uint32_t)(count*1000/sample_rate) : 0;
        ndspChnSetRate(CHANNEL,(float)sample_rate);
        ndspChnWaveBufClear(CHANNEL); ndspChnSetPaused(CHANNEL,true);
        eof=false;
        int queued=0;
        for (int i=0;i<BLOCK_COUNT && !eof;++i) queued+=queue_initial(i);
        if (!queued) { set_error("Audio vide ou illisible."); audio_stop(); return false; }
        active=true;
        ndspChnSetPaused(CHANNEL,false); return true;
    }
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
    for (int i = 0; i < BLOCK_COUNT && !eof; ++i) queued += queue_initial(i);
    if (!queued || !decoded_any) {
        if (!*error_text) set_error("Ce fichier ne contient pas d'audio MP3 lisible.");
        audio_stop(); return false;
    }
    active = true;
    ndspChnSetPaused(CHANNEL, false);
    return true;

open_aac:
    file=fopen(path,"rb");
    if (!file) { set_error("Audio absent sur la carte SD."); audio_stop(); return false; }
    aac_decoder=NeAACDecOpen();
    if (!aac_decoder) { set_error("Memoire AAC insuffisante."); audio_stop(); return false; }
    NeAACDecConfigurationPtr config=NeAACDecGetCurrentConfiguration(aac_decoder);
    if (!config) { set_error("Initialisation AAC impossible."); audio_stop(); return false; }
    config->defObjectType=LC;
    config->outputFormat=FAAD_FMT_16BIT;
    if (!NeAACDecSetConfiguration(aac_decoder,config)) {
        set_error("Configuration AAC impossible."); audio_stop(); return false;
    }
    unsigned long rate=0; unsigned char channels=0;
    if (mode==4) {
        if (fseek(file,0,SEEK_END)!=0) { set_error("M4A illisible."); audio_stop(); return false; }
        long size=ftell(file);
        if (size<=0 || !MP4D_open(&mp4,mp4_read,file,size)) {
            set_error("M4A : index absent de l'extrait ou fichier illisible.");
            audio_stop(); return false;
        }
        mp4_open=true; bool found=false;
        for (unsigned i=0;i<mp4.track_count;++i) {
            MP4D_track_t *track=&mp4.track[i];
            if (track->sample_count && track->sample_count<1000000 && track->dsi &&
                track->dsi_bytes>=2 && track->dsi_bytes<=128 &&
                (track->object_type_indication==0x40 ||
                 (track->object_type_indication>=0x66 && track->object_type_indication<=0x68))) {
                mp4_track=i; found=true; break;
            }
        }
        if (!found) {
            set_error("M4A : codec non compatible (AAC requis, ALAC exclu).");
            audio_stop(); return false;
        }
        if (NeAACDecInit2(aac_decoder,mp4.track[mp4_track].dsi,
                mp4.track[mp4_track].dsi_bytes,&rate,&channels)!=0) {
            set_error("M4A : profil AAC non reconnu."); audio_stop(); return false;
        }
        MP4D_track_t *track=&mp4.track[mp4_track];
        duration_ms=known_duration_ms;
        if (!duration_ms && track->timescale && track->duration_lo) {
            uint64_t value=((uint64_t)track->duration_lo*1000)/track->timescale;
            duration_ms=value<=UINT32_MAX ? (uint32_t)value : 0;
        }
    } else {
        input_size=(int)fread(input,1,INPUT_BYTES,file);
        long consumed=NeAACDecInit(aac_decoder,input,(unsigned long)input_size,&rate,&channels);
        if (consumed<0 || consumed>input_size) {
            set_error("Flux AAC illisible."); audio_stop(); return false;
        }
        memmove(input,input+consumed,(size_t)(input_size-consumed));
        input_size-=(int)consumed;
        duration_ms=known_duration_ms;
    }
    if (channels<1 || channels>2 || rate<8000 || rate>48000) {
        set_error("AAC : canaux ou frequence incompatibles."); audio_stop(); return false;
    }
    sample_rate=(unsigned)rate; decoded_any=false; eof=false;
    ndspChnSetRate(CHANNEL,(float)sample_rate);
    ndspChnWaveBufClear(CHANNEL); ndspChnSetPaused(CHANNEL,true);
    int aac_queued=0;
    for (int i=0;i<BLOCK_COUNT && !eof;++i) aac_queued+=queue_initial(i);
    if (!aac_queued || !decoded_any) {
        if (!*error_text) set_error("Aucun audio AAC lisible.");
        audio_stop(); return false;
    }
    active=true;
    ndspChnSetPaused(CHANNEL,false); return true;
}

bool audio_open(const char *path,uint32_t known_duration_ms) {
    return audio_open_impl(path,known_duration_ms,false);
}

bool audio_open_stream(const char *path,uint32_t known_duration_ms) {
    const char *extension=strrchr(path,'.');
    if (!extension || (strcasecmp(extension,".mp3") && strcasecmp(extension,".aac")))
        return audio_open(path,known_duration_ms);
    return audio_open_impl(path,known_duration_ms,true);
}

void audio_set_streaming(bool incoming) {
    streaming_input=incoming;
    if (file) clearerr(file);
}

void audio_toggle_pause(void) {
    if (!active) return;
    paused = !paused;
    ndspChnSetPaused(CHANNEL, paused || stream_buffering);
}

void audio_update(void) {
    if (!active || !ready) return;
    uint64_t now = osGetTime();
    if (paused) return;
    if (stream_buffering && now<stream_retry_after) return;
    for (int i = 0; i < BLOCK_COUNT; ++i) {
        if (blocks[i].wave.status == NDSP_WBUF_DONE) {
            ++consumed_blocks;
            played_frames += blocks[i].wave.nsamples;
            blocks[i].wave.status = NDSP_WBUF_FREE;
            if (!eof) queue(&blocks[i]);
        } else if (stream_buffering && !eof &&
                   blocks[i].wave.status == NDSP_WBUF_FREE) {
            /* Retry an empty buffer when new network bytes arrive. */
            queue(&blocks[i]);
        }
    }
    bool pending_wave=false;
    for (int i=0;i<BLOCK_COUNT;++i)
        if (blocks[i].wave.status == NDSP_WBUF_QUEUED ||
            blocks[i].wave.status == NDSP_WBUF_PLAYING) pending_wave=true;
    if (streaming_input && !pending_wave) {
        stream_buffering=true;
        stream_retry_after=now+30;
        ndspChnSetPaused(CHANNEL,true);
    } else if (stream_buffering && pending_wave) {
        stream_buffering=false;
        ndspChnSetPaused(CHANNEL,false);
    }
    if (eof && !pending_wave) {
        audio_stop();
    }
}

bool audio_playing(void) { return active; }
bool audio_paused(void) { return paused; }
bool audio_buffering(void) { return active && stream_buffering; }
uint32_t audio_position_ms(void) {
    /* Count only PCM buffers actually played by NDSP. Network stalls and
       buffering must not consume the preview's audible time budget. */
    uint64_t value = sample_rate ? played_frames * 1000 / sample_rate : 0;
    if (duration_ms && value > duration_ms) value = duration_ms;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}
uint32_t audio_duration_ms(void) { return duration_ms; }
const char *audio_error(void) { return error_text; }
unsigned audio_initial_peak(void) { return initial_peak; }
unsigned audio_initial_frames(void) { return initial_frames; }
unsigned audio_consumed_blocks(void) { return consumed_blocks; }
unsigned audio_sample_rate(void) { return sample_rate; }
