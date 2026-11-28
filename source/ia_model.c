#include "ia_model.h"
#include "cJSON.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_text(char *out, size_t cap, const char *text) {
    if (!cap) return;
    snprintf(out, cap, "%s", text ? text : "");
}

static const char *json_text(const cJSON *object, const char *key) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(field)) return field->valuestring;
    if (cJSON_IsArray(field) && cJSON_GetArraySize(field) > 0) {
        field = cJSON_GetArrayItem(field, 0);
        if (cJSON_IsString(field)) return field->valuestring;
    }
    return NULL;
}

static uint64_t json_uint(const cJSON *object, const char *key) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(field)) {
        if (field->valuedouble >= 0 && field->valuedouble < 9007199254740992.0)
            return (uint64_t)field->valuedouble;
        return 0;
    }
    if (!cJSON_IsString(field) || !field->valuestring) return 0;
    const char *s = field->valuestring;
    if (!*s || *s == '-') return 0;
    char *end = NULL;
    unsigned long long value = strtoull(s, &end, 10);
    return end && end!=s && !*end ? (uint64_t)value : 0;
}

static bool identifier_valid(const char *id) {
    if (!id || !*id || strlen(id) >= 128) return false;
    if (!strcmp(id, ".") || !strcmp(id, "..")) return false;
    for (const unsigned char *p=(const unsigned char *)id; *p; ++p)
        if (!isalnum(*p) && *p!='-' && *p!='_' && *p!='.' && *p!='@' && *p!='+') return false;
    return true;
}

static bool filename_valid(const char *name) {
    if (!name || !*name || strlen(name) >= 256 || name[0]=='/') return false;
    const char *part=name;
    for (const unsigned char *p=(const unsigned char *)name; ; ++p) {
        if (*p=='\\' || (*p && (*p<32 || *p==127))) return false;
        if (*p=='/' || !*p) {
            size_t n=(const char *)p-part;
            if (!n || (n==1 && part[0]=='.') ||
                (n==2 && part[0]=='.' && part[1]=='.')) return false;
            if (!*p) return true;
            part=(const char *)p+1;
        }
    }
}

static bool url_append_encoded(char *out, size_t cap, size_t *at, const char *value,
                               bool keep_slash) {
    const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char *)value; *p; ++p) {
        unsigned char c=*p;
        bool plain = isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~' ||
                     (keep_slash && c=='/');
        if (*at + (plain ? 1u : 3u) >= cap) return false;
        if (plain) out[(*at)++]=(char)c;
        else { out[(*at)++]='%'; out[(*at)++]=hex[c>>4]; out[(*at)++]=hex[c&15]; }
    }
    out[*at]=0;
    return true;
}

static bool url_append_literal(char *out,size_t cap,size_t *at,const char *value) {
    size_t length=strlen(value);
    if (length>=cap-*at) return false;
    memcpy(out+*at,value,length+1);
    *at+=length;
    return true;
}

static bool append_search_terms(char *out,size_t cap,size_t *used,const char *clean) {
    const char *part=clean;
    bool first=true;
    while (*part) {
        while (*part==' ') ++part;
        if (!*part) break;
        const char *end=part;
        while (*end && *end!=' ') ++end;
        char word[96]; size_t length=(size_t)(end-part);
        if (!length || length>=sizeof(word)) return false;
        memcpy(word,part,length); word[length]=0;
        if ((!first && !url_append_literal(out,cap,used,"%20AND%20")) ||
            !url_append_literal(out,cap,used,"%22") ||
            !url_append_encoded(out,cap,used,word,false) ||
            !url_append_literal(out,cap,used,"%22")) return false;
        first=false; part=end;
    }
    return !first;
}

bool ia_build_search_url(const char *query, unsigned page, char *out, size_t cap) {
    if (!out || cap<80 || !query || !*query || page<1 || page>2500) return false;
    char clean[96]; size_t at=0;
    for (const unsigned char *p=(const unsigned char *)query; *p && at<sizeof(clean)-1; ++p) {
        if (*p<32 || *p==127) continue;
        if (*p=='"' || *p=='\\' || *p==':' || *p=='(' || *p==')' || *p=='[' || *p==']') continue;
        clean[at++]=(char)*p;
    }
    while (at && clean[at-1]==' ') --at;
    clean[at]=0;
    if (!at) return false;
    /* All words must occur in title or creator, in any order. An exact phrase
       missed titles such as "Splatoon 3 Original Soundtrack" when asked for
       "Splatoon soundtrack". */
    const char *prefix="https://archive.org/advancedsearch.php?q=mediatype%3Aaudio%20AND%20%28title%3A%28";
    int n=snprintf(out,cap,"%s",prefix);
    if (n<0 || (size_t)n>=cap) return false;
    size_t used=(size_t)n;
    if (!append_search_terms(out,cap,&used,clean) ||
        !url_append_literal(out,cap,&used,"%29%20OR%20creator%3A%28") ||
        !append_search_terms(out,cap,&used,clean) ||
        !url_append_literal(out,cap,&used,"%29%29")) return false;
    /* Explicit ordering keeps adjacent pages stable. The API's implicit order
       varies between requests; popularity also puts complete music archives
       ahead of most incidental podcast mentions for typical music searches. */
    n=snprintf(out+used,cap-used,"&fl%%5B%%5D=identifier&fl%%5B%%5D=title&fl%%5B%%5D=creator&fl%%5B%%5D=date&rows=4&page=%u&output=json&sort%%5B%%5D=downloads%%20desc&sort%%5B%%5D=identifier%%20asc",page);
    return n>=0 && (size_t)n<cap-used;
}

bool ia_build_metadata_url(const char *identifier, char *out, size_t cap) {
    if (!identifier_valid(identifier) || !out) return false;
    size_t used=(size_t)snprintf(out,cap,"https://archive.org/metadata/");
    return used<cap && url_append_encoded(out,cap,&used,identifier,false);
}

bool ia_build_file_url(const char *identifier, const char *filename,
                       char *out, size_t cap) {
    if (!identifier_valid(identifier) || !filename_valid(filename) || !out) return false;
    int n=snprintf(out,cap,"https://archive.org/download/");
    if (n<0 || (size_t)n>=cap) return false;
    size_t used=(size_t)n;
    if (!url_append_encoded(out,cap,&used,identifier,false) || used+1>=cap) return false;
    out[used++]='/'; out[used]=0;
    return url_append_encoded(out,cap,&used,filename,true);
}

bool ia_build_cover_url(const char *identifier, const char *filename,
                        char *out, size_t cap) {
    if (filename && *filename) return ia_build_file_url(identifier,filename,out,cap);
    if (!identifier_valid(identifier) || !out) return false;
    int n=snprintf(out,cap,"https://archive.org/services/img/");
    if (n<0 || (size_t)n>=cap) return false;
    size_t used=(size_t)n;
    return url_append_encoded(out,cap,&used,identifier,false);
}

static bool audio_extension(const char *name, IaAudioFormat *format) {
    const char *dot=strrchr(name,'.');
    if (!dot) return false;
    if (!strcasecmp(dot,".mp3")) *format=IA_AUDIO_MP3;
    else if (!strcasecmp(dot,".flac")) *format=IA_AUDIO_FLAC;
    else if (!strcasecmp(dot,".wav")) *format=IA_AUDIO_WAV;
    else if (!strcasecmp(dot,".aac")) *format=IA_AUDIO_AAC;
    else if (!strcasecmp(dot,".m4a")) *format=IA_AUDIO_M4A;
    else return false;
    return true;
}

const char *ia_format_name(IaAudioFormat f) {
    switch (f) {
        case IA_AUDIO_MP3: return "MP3";
        case IA_AUDIO_FLAC: return "FLAC";
        case IA_AUDIO_WAV: return "WAV";
        case IA_AUDIO_AAC: return "AAC";
        case IA_AUDIO_M4A: return "M4A";
    }
    return "?";
}

size_t ia_preview_range_bytes(const IaTrack *track, unsigned seconds) {
    const size_t three_mb=3u*1024u*1024u;
    const size_t four_mb=4u*1024u*1024u;
    if (seconds!=30 && seconds!=50) seconds=30;
    if (!track) return four_mb;
    bool lossless=track->format==IA_AUDIO_WAV || track->format==IA_AUDIO_FLAC;
    size_t ceiling=lossless ? (seconds==50 ? 16u : 10u)*1024u*1024u :
        (seconds==50 ? 5u : 3u)*1024u*1024u;
    if (track->format==IA_AUDIO_M4A && track->bytes && track->bytes<=four_mb)
        return four_mb;
    if (track->duration_ms<=seconds*1000u || !track->bytes ||
        track->bytes>512u*1024u*1024u) return lossless ? ceiling : three_mb;
    /* A small time margin plus header/index space for the requested preview.
       Metadata can be inaccurate, so keep a substantial minimum. */
    uint64_t estimate=track->bytes*(seconds+(lossless ? 3u : 10u))*1000u/
        track->duration_ms+256u*1024u;
    size_t minimum=lossless ? 2u*1024u*1024u :
        track->format==IA_AUDIO_MP3 ? 768u*1024u : 1024u*1024u;
    if (estimate<minimum) estimate=minimum;
    if (estimate>ceiling) estimate=ceiling;
    return (size_t)estimate;
}

static uint32_t duration_ms(const cJSON *file) {
    const char *text=json_text(file,"length");
    if (!text) return 0;
    unsigned h=0,m=0; double s=0;
    if (strchr(text,':')) {
        if (sscanf(text,"%u:%u:%lf",&h,&m,&s)==3) s+=h*3600.0+m*60.0;
        else if (sscanf(text,"%u:%lf",&m,&s)==2) s+=m*60.0;
        else return 0;
    } else {
        char *end=NULL; s=strtod(text,&end);
        if (!end || *end) return 0;
    }
    return s>0 && s<(double)UINT32_MAX/1000.0 ? (uint32_t)(s*1000.0+0.5) : 0;
}

static void archive_fields(IaArchive *out, const cJSON *object) {
    copy_text(out->identifier,sizeof(out->identifier),json_text(object,"identifier"));
    copy_text(out->title,sizeof(out->title),json_text(object,"title"));
    copy_text(out->creator,sizeof(out->creator),json_text(object,"creator"));
    copy_text(out->archive_date,sizeof(out->archive_date),json_text(object,"date"));
    if (!out->title[0]) copy_text(out->title,sizeof(out->title),out->identifier);
}

static bool title_has(const char *title, const char *word, bool whole_word) {
    size_t length=strlen(word);
    for (size_t i=0;title[i];++i) {
        size_t j=0;
        while (j<length && title[i+j] &&
               tolower((unsigned char)title[i+j])==tolower((unsigned char)word[j])) ++j;
        if (j==length && (!whole_word ||
            ((i==0 || !isalnum((unsigned char)title[i-1])) &&
             !isalnum((unsigned char)title[i+j])))) return true;
    }
    return false;
}

static int music_score(const IaArchive *archive) {
    const char *title=archive->title;
    int score=0;
    if (title_has(title,"ost",true)) score+=8;
    if (title_has(title,"soundtrack",false)) score+=7;
    if (title_has(title,"album",false)) score+=4;
    if (title_has(title,"music",false)) score+=2;
    if (title_has(title,"sound effect",false) || title_has(title,"sfx",true)) score-=8;
    if (title_has(title,"podcast",false)) score-=5;
    return score;
}

bool ia_parse_search(const char *json, size_t size, unsigned page, IaSearchPage *out) {
    if (!json || !out || !size || size>1048576 || page<1) return false;
    memset(out,0,sizeof(*out));
    cJSON *root=cJSON_ParseWithLength(json,size);
    if (!root) return false;
    const cJSON *response=cJSON_GetObjectItemCaseSensitive(root,"response");
    const cJSON *docs=cJSON_GetObjectItemCaseSensitive(response,"docs");
    if (!cJSON_IsObject(response) || !cJSON_IsArray(docs)) { cJSON_Delete(root); return false; }
    uint64_t total=json_uint(response,"numFound");
    out->total=total>UINT_MAX ? UINT_MAX : (unsigned)total;
    out->page=page;
    cJSON *entry=NULL;
    cJSON_ArrayForEach(entry,docs) {
        if (out->count==IA_PAGE_ITEMS) break;
        IaArchive item={0}; archive_fields(&item,entry);
        if (!identifier_valid(item.identifier)) continue;
        out->archives[out->count++]=item;
    }
    /* Only the four visible results are reordered. No extra network request,
       metadata scan or work is added to the render loop. Equal scores retain
       the server's popularity order. */
    for (unsigned i=1;i<out->count;++i) {
        IaArchive item=out->archives[i];
        int score=music_score(&item);
        unsigned j=i;
        while (j>0 && music_score(&out->archives[j-1])<score) {
            out->archives[j]=out->archives[j-1];
            --j;
        }
        out->archives[j]=item;
    }
    cJSON_Delete(root);
    return true;
}

static bool picture_extension(const char *name) {
    const char *dot=strrchr(name,'.');
    return dot && (!strcasecmp(dot,".jpg") || !strcasecmp(dot,".jpeg") ||
                   !strcasecmp(dot,".png"));
}

static size_t stem_length(const char *name) {
    const char *dot=strrchr(name,'.');
    return dot ? (size_t)(dot-name) : strlen(name);
}

bool ia_parse_item(const char *json, size_t size, const char *identifier, IaItem *out) {
    if (!json || !out || !identifier_valid(identifier) || !size || size>2097152) return false;
    memset(out,0,sizeof(*out));
    cJSON *root=cJSON_ParseWithLength(json,size);
    if (!root) return false;
    const cJSON *metadata=cJSON_GetObjectItemCaseSensitive(root,"metadata");
    const cJSON *files=cJSON_GetObjectItemCaseSensitive(root,"files");
    if (!cJSON_IsObject(metadata) || !cJSON_IsArray(files)) { cJSON_Delete(root); return false; }
    archive_fields(&out->archive,metadata);
    copy_text(out->archive.identifier,sizeof(out->archive.identifier),identifier);
    if (!out->archive.title[0]) copy_text(out->archive.title,sizeof(out->archive.title),identifier);
    const cJSON *file=NULL;
    cJSON_ArrayForEach(file,files) {
        const char *name=json_text(file,"name"); IaAudioFormat format;
        if (!name || !filename_valid(name) || !audio_extension(name,&format)) continue;
        ++out->total_audio_files;
        if (out->count==IA_MAX_TRACKS) { out->truncated=true; continue; }
        IaTrack *track=&out->tracks[out->count++];
        copy_text(track->name,sizeof(track->name),name);
        const char *title=json_text(file,"title");
        if (title && *title) copy_text(track->title,sizeof(track->title),title);
        else {
            const char *base=strrchr(name,'/'); base=base ? base+1 : name;
            size_t n=stem_length(base); if (n>=sizeof(track->title)) n=sizeof(track->title)-1;
            memcpy(track->title,base,n); track->title[n]=0;
        }
        track->format=format;
        track->bytes=json_uint(file,"size");
        track->duration_ms=duration_ms(file);
        copy_text(track->date,sizeof(track->date),json_text(file,"date"));
        if (!track->date[0]) copy_text(track->date,sizeof(track->date),json_text(file,"year"));
        const cJSON *private_value=cJSON_GetObjectItemCaseSensitive(file,"private");
        const char *priv=json_text(file,"private");
        track->downloadable = !cJSON_IsTrue(private_value) &&
            (!priv || (strcmp(priv,"true") && strcmp(priv,"1")));
    }
    /* An image with the same basename is a defensible track-specific match.
       Otherwise the UI reuses the archive cover, never treating tracks as duplicates. */
    cJSON_ArrayForEach(file,files) {
        const char *name=json_text(file,"name");
        if (!name || !filename_valid(name) || !picture_extension(name)) continue;
        size_t image_stem=stem_length(name);
        for (unsigned i=0;i<out->count;++i) {
            IaTrack *track=&out->tracks[i];
            size_t audio_stem=stem_length(track->name);
            if (!track->cover_file[0] && audio_stem==image_stem &&
                !strncasecmp(track->name,name,image_stem))
                copy_text(track->cover_file,sizeof(track->cover_file),name);
        }
    }
    cJSON_Delete(root);
    return true;
}

unsigned ia_page_count(unsigned total) {
    return total/IA_PAGE_ITEMS + (total%IA_PAGE_ITEMS != 0);
}
unsigned ia_page_first(unsigned page) { return page ? (page-1)*IA_PAGE_ITEMS : 0; }
