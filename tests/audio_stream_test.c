#include "audio.h"
#include "audio_host_stub.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

int main(int argc,char **argv) {
    if (argc!=3) return 2;
    FILE *source=fopen(argv[1],"rb"), *part=fopen(argv[2],"wb");
    CHECK(source && part);
    unsigned char buffer[32768];
    size_t first=fread(buffer,1,sizeof(buffer),source);
    CHECK(first==sizeof(buffer));
    CHECK(fwrite(buffer,1,first,part)==first);
    CHECK(fclose(part)==0);
    CHECK(audio_init());
    CHECK(audio_open_stream(argv[2],0));
    CHECK(audio_initial_peak()>0);
    for (int i=0;i<1000 && !audio_buffering();++i) {
        fake_drain_one(); audio_update();
    }
    CHECK(audio_buffering());
    uint32_t held=audio_position_ms();
    for (int i=0;i<20;++i) audio_update();
    CHECK(audio_position_ms()==held);
    part=fopen(argv[2],"ab"); CHECK(part);
    size_t got;
    while ((got=fread(buffer,1,sizeof(buffer),source))>0)
        CHECK(fwrite(buffer,1,got,part)==got);
    CHECK(fclose(part)==0);
    fclose(source);
    for (int i=0;i<500 && fake_total_frames()<120000;++i) {
        fake_drain_one(); audio_update();
    }
    CHECK(fake_total_frames()>100000);
    audio_set_streaming(false);
    for (int i=0;i<10000 && audio_playing();++i) {
        fake_drain_one(); audio_update();
    }
    CHECK(!audio_playing());
    fprintf(stderr,"decoded_frames=%llu\n",(unsigned long long)fake_total_frames());
    CHECK(fake_total_frames()>100000);
    printf("stream: %llu frames, buffering and resume OK\n",
        (unsigned long long)fake_total_frames());
    audio_exit();
    remove(argv[2]);
    return 0;
}
