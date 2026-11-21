#ifndef AUDIO_HOST_STUB_H
#define AUDIO_HOST_STUB_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { void *data_vaddr; int nsamples; int status; } ndspWaveBuf;
enum { NDSP_WBUF_DONE=0, NDSP_WBUF_QUEUED=1, NDSP_WBUF_PLAYING=2 };
enum { NDSP_OUTPUT_STEREO=2, NDSP_INTERP_POLYPHASE=2, NDSP_FORMAT_STEREO_PCM16=2 };
#define R_FAILED(value) ((value)<0)
int ndspInit(void);
void ndspExit(void);
void ndspSetOutputMode(int mode);
void ndspChnReset(int channel);
void ndspChnSetInterp(int channel,int interp);
void ndspChnSetFormat(int channel,int format);
void ndspChnSetMix(int channel,const float mix[12]);
void ndspChnSetRate(int channel,float rate);
void ndspChnSetPaused(int channel,bool pause);
void ndspChnWaveBufClear(int channel);
void ndspChnWaveBufAdd(int channel,ndspWaveBuf *wave);
void DSP_FlushDataCache(void *data,size_t bytes);
void *linearAlloc(size_t bytes);
void linearFree(void *data);
void aptSetSleepAllowed(bool allowed);
uint64_t osGetTime(void);
bool fake_drain_one(void);
uint64_t fake_total_frames(void);
#endif
