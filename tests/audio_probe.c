#include "audio.h"
#include "audio_host_stub.h"
#include <stdio.h>

int main(int argc,char **argv) {
    if (argc!=2 || !audio_init()) return 2;
    if (!audio_open(argv[1],0)) {
        fprintf(stderr,"open failed: %s\n",audio_error()); audio_exit(); return 1;
    }
    for (int i=0;i<10000 && audio_playing();++i) {
        fake_drain_one(); audio_update();
    }
    printf("decoded %llu frames; peak PCM %u; error: %s\n",
           (unsigned long long)fake_total_frames(),fake_peak_pcm(),audio_error());
    audio_exit(); return 0;
}
