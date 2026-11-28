#include "ia_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int test_search(void) {
    const char *json="{\"response\":{\"numFound\":7,\"docs\":[{\"identifier\":\"valid-item\",\"title\":\"Known title\"},{\"identifier\":\"../bad\"},{\"identifier\":\"second\"}]}}";
    IaSearchPage page;
    CHECK(ia_parse_search(json,strlen(json),2,&page));
    CHECK(page.total==7 && page.count==2 && page.page==2);
    CHECK(!strcmp(page.archives[0].title,"Known title"));
    CHECK(!strcmp(page.archives[1].title,"second"));
    CHECK(ia_page_count(7)==2 && ia_page_first(2)==4);
    CHECK(!ia_parse_search("{}",2,1,&page));
    const char *ranked="{\"response\":{\"numFound\":4,\"docs\":["
        "{\"identifier\":\"one\",\"title\":\"Splatoon sound effects\"},"
        "{\"identifier\":\"two\",\"title\":\"Splatoon Original Soundtrack\"},"
        "{\"identifier\":\"three\",\"title\":\"Splatoon OST\"},"
        "{\"identifier\":\"four\",\"title\":\"Splatoon podcast\"}]}}";
    CHECK(ia_parse_search(ranked,strlen(ranked),1,&page));
    CHECK(!strcmp(page.archives[0].identifier,"three"));
    CHECK(!strcmp(page.archives[1].identifier,"two"));
    CHECK(!strcmp(page.archives[2].identifier,"four"));
    CHECK(!strcmp(page.archives[3].identifier,"one"));
    return 0;
}

static int test_item(void) {
    const char *json="{\"metadata\":{\"title\":\"Collection\"},\"files\":["
      "{\"name\":\"disc/one.mp3\",\"size\":\"1200\",\"length\":\"1:02.5\",\"date\":\"2023\"},"
      "{\"name\":\"disc/one.jpg\"},"
      "{\"name\":\"two.flac\",\"size\":\"2000\"},"
      "{\"name\":\"three.wav\"},"
      "{\"name\":\"four.aac\"},"
      "{\"name\":\"five.m4a\"},"
      "{\"name\":\"secret.mp3\",\"private\":true},"
      "{\"name\":\"../escape.mp3\"}]}";
    IaItem *item=calloc(1,sizeof(*item)); CHECK(item);
    CHECK(ia_parse_item(json,strlen(json),"test-item",item));
    CHECK(item->count==6 && item->total_audio_files==6);
    CHECK(!strcmp(item->archive.title,"Collection"));
    CHECK(item->tracks[0].duration_ms==62500);
    CHECK(item->tracks[1].duration_ms==0 && !item->tracks[1].date[0]);
    CHECK(!strcmp(item->tracks[0].cover_file,"disc/one.jpg"));
    CHECK(!item->tracks[5].downloadable);
    CHECK(item->tracks[4].format==IA_AUDIO_M4A);
    const char *empty="{\"metadata\":{\"title\":\"Video only\"},\"files\":[{\"name\":\"movie.mp4\"}]}";
    CHECK(ia_parse_item(empty,strlen(empty),"video-only",item));
    CHECK(item->count==0 && item->total_audio_files==0);
    free(item);
    return 0;
}

static int test_urls(void) {
    char url[512];
    CHECK(ia_build_search_url("Hello World",1,url,sizeof(url)));
    CHECK(strstr(url,"title%3A%28%22Hello%22%20AND%20%22World%22%29%20OR%20creator%3A%28%22Hello%22%20AND%20%22World%22%29"));
    CHECK(strstr(url,"rows=4"));
    CHECK(!strstr(url,"q=mediatype%3Aaudio%20AND%20Hello"));
    CHECK(strstr(url,"sort%5B%5D=downloads%20desc"));
    CHECK(strstr(url,"sort%5B%5D=identifier%20asc"));
    CHECK(ia_build_file_url("a-b","x/y song.mp3",url,sizeof(url)));
    CHECK(!strcmp(url,"https://archive.org/download/a-b/x/y%20song.mp3"));
    CHECK(!ia_build_file_url("../x","f.mp3",url,sizeof(url)));
    CHECK(!ia_build_file_url("x","a/../b.mp3",url,sizeof(url)));
    CHECK(ia_build_cover_url("x",NULL,url,sizeof(url)));
    CHECK(!strcmp(url,"https://archive.org/services/img/x"));
    return 0;
}

static int test_preview_limit(void) {
    IaTrack track={.format=IA_AUDIO_M4A,.bytes=20u*1024u*1024u,
                   .duration_ms=20u*60u*1000u};
    size_t limit=ia_preview_range_bytes(&track,30);
    CHECK(limit>=1024u*1024u && limit<3u*1024u*1024u);
    CHECK(ia_preview_range_bytes(&track,50)>=limit);
    track.bytes=2u*1024u*1024u;
    CHECK(ia_preview_range_bytes(&track,30)==4u*1024u*1024u);
    track.format=IA_AUDIO_MP3;
    CHECK(ia_preview_range_bytes(&track,30)>=768u*1024u);
    track.format=IA_AUDIO_FLAC;
    CHECK(ia_preview_range_bytes(&track,30)>=2u*1024u*1024u);
    track.bytes=200u*1024u*1024u;
    track.duration_ms=20u*60u*1000u;
    CHECK(ia_preview_range_bytes(&track,50)>ia_preview_range_bytes(&track,30));
    return 0;
}

int main(void) {
    if (test_search() || test_item() || test_urls() || test_preview_limit()) return 1;
    puts("IA model tests passed");
    return 0;
}
