/* Independent Old 3DS audio-output check. No network, files or decoder. */
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RATE 32000
#define FRAMES 32000

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    puts("SHIELD NDSP output check\nA: 1 s tone   START: exit\n");
    Result result=ndspInit();
    printf("ndspInit: %08lX\n",(unsigned long)result);
    if (R_FAILED(result)) {
        puts("DSP unavailable. Check dspfirm.cdc.");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown()&KEY_START) break; gspWaitForVBlank(); }
        gfxExit(); return 1;
    }
    int16_t *pcm=linearAlloc(FRAMES*2*sizeof(int16_t));
    printf("linearAlloc: %s\n",pcm ? "OK" : "FAILED");
    if (!pcm) { ndspExit(); gfxExit(); return 1; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0,(float)RATE);
    ndspChnSetInterp(0,NDSP_INTERP_POLYPHASE);
    float mix[12]={0}; mix[0]=mix[1]=0.35f;
    ndspChnSetMix(0,mix);
    for (unsigned i=0;i<FRAMES;++i) {
        float envelope=(i<800 ? i/800.0f : i>FRAMES-800 ? (FRAMES-i)/800.0f : 1.0f);
        int16_t sample=(int16_t)(10000.0f*envelope*sinf(2.0f*3.14159265f*440.0f*i/RATE));
        pcm[i*2]=pcm[i*2+1]=sample;
    }
    DSP_FlushDataCache(pcm,FRAMES*2*sizeof(int16_t));
    ndspWaveBuf wave={0}; wave.data_vaddr=pcm; wave.nsamples=FRAMES;
    bool queued=false;
    while (aptMainLoop()) {
        hidScanInput(); u32 keys=hidKeysDown();
        if (keys&KEY_START) break;
        if ((keys&KEY_A) && !queued) {
            ndspChnSetPaused(0,true);
            ndspChnWaveBufAdd(0,&wave);
            ndspChnSetPaused(0,false);
            queued=true;
            printf("Submitted %u stereo frames; status=%d\n",FRAMES,wave.status);
        }
        if (queued && wave.status==NDSP_WBUF_DONE) {
            puts("DSP consumed buffer. Press A again.");
            queued=false;
        }
        gspWaitForVBlank();
    }
    ndspChnWaveBufClear(0);
    ndspChnReset(0);
    linearFree(pcm);
    ndspExit(); gfxExit(); return 0;
}
