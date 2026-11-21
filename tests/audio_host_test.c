#include "audio.h"
#include "audio_host_stub.h"
#include <assert.h>
#include <stdio.h>

int main(int argc,char **argv) {
    if (argc!=2) return 2;
    assert(audio_init());
    assert(!audio_open("missing-test-file.mp3",0));
    assert(*audio_error());
    assert(audio_open(argv[1],0));
    assert(audio_duration_ms()>3000 && audio_duration_ms()<5000);
    assert(audio_playing());
    audio_toggle_pause(); assert(audio_paused());
    audio_toggle_pause(); assert(!audio_paused());
    for (int i=0;i<10000 && audio_playing();++i) {
        fake_drain_one();
        audio_update();
    }
    assert(!audio_playing());
    assert(fake_total_frames()>80000);
    printf("audio: decoded %llu sample frames, pause/reprise/fin OK\n",
           (unsigned long long)fake_total_frames());
    audio_exit();
    return 0;
}
