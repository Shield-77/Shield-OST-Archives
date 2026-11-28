#include "audio_host_stub.h"
#include <string.h>
static ndspWaveBuf *waves[128];
static int wave_count;
static uint64_t total_frames, tick;
static unsigned peak_pcm;
int ndspInit(void) { return 0; }
void ndspExit(void) {}
void ndspSetOutputMode(int mode) { (void)mode; }
void ndspSetMasterVol(float volume) { (void)volume; }
void ndspSetClippingMode(int mode) { (void)mode; }
void ndspChnReset(int channel) { (void)channel; }
void ndspChnSetInterp(int channel,int interp) { (void)channel; (void)interp; }
void ndspChnSetFormat(int channel,int format) { (void)channel; (void)format; }
void ndspChnSetMix(int channel,const float mix[12]) { (void)channel; (void)mix; }
void ndspChnSetRate(int channel,float rate) { (void)channel; (void)rate; }
void ndspChnSetPaused(int channel,bool pause) { (void)channel; (void)pause; }
void ndspChnWaveBufClear(int channel) {
    (void)channel;
    for (int i=0;i<wave_count;++i) waves[i]->status=NDSP_WBUF_DONE;
    wave_count=0;
}
void ndspChnWaveBufAdd(int channel,ndspWaveBuf *wave) {
    (void)channel;
    wave->status=NDSP_WBUF_QUEUED;
    if (wave_count<128) waves[wave_count++]=wave;
}
void DSP_FlushDataCache(void *data,size_t bytes) { (void)data; (void)bytes; }
void *linearAlloc(size_t bytes) { return malloc(bytes); }
void linearFree(void *data) { free(data); }
void aptSetSleepAllowed(bool allowed) { (void)allowed; }
uint64_t osGetTime(void) { tick+=17; return tick; }
bool fake_drain_one(void) {
    for (int i=0;i<wave_count;++i) {
        if (waves[i]->status == NDSP_WBUF_QUEUED) {
            const int16_t *pcm=(const int16_t *)waves[i]->data_vaddr;
            for (int sample=0; sample<waves[i]->nsamples*2; ++sample) {
                int value=pcm[sample];
                unsigned amplitude=(unsigned)(value<0 ? -value : value);
                if (amplitude>peak_pcm) peak_pcm=amplitude;
            }
            total_frames += waves[i]->nsamples;
            waves[i]->status=NDSP_WBUF_DONE;
            return true;
        }
    }
    return false;
}
uint64_t fake_total_frames(void) { return total_frames; }
unsigned fake_peak_pcm(void) { return peak_pcm; }
