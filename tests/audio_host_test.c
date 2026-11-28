#include "audio.h"
#include "audio_host_stub.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

int main(int argc,char **argv) {
    if (argc!=2) return 2;
    CHECK(audio_init());
    CHECK(!audio_open("missing-test-file.mp3",0));
    CHECK(*audio_error());
    CHECK(audio_open(argv[1],0));
    CHECK(audio_initial_frames()>0);
    CHECK(audio_initial_peak()>0);
    CHECK(!audio_duration_ms() || audio_duration_ms()>1000);
    CHECK(audio_playing());
    audio_toggle_pause(); CHECK(audio_paused());
    audio_toggle_pause(); CHECK(!audio_paused());
    for (int i=0;i<10000 && audio_playing();++i) {
        fake_drain_one();
        audio_update();
    }
    CHECK(!audio_playing());
    CHECK(fake_total_frames()>80000);
    CHECK(audio_consumed_blocks()>0);
    printf("audio: decoded %llu sample frames, pause/reprise/fin OK\n",
           (unsigned long long)fake_total_frames());
    audio_exit();
    return 0;
}
