#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include "audio.h"
#include "ia_http.h"
#include "ia_model.h"
#include "cover.h"
#include "i18n.h"

enum { VIEW_SEARCH, VIEW_TRACKS, VIEW_OPTIONS, VIEW_GUIDE, VIEW_QUEUE, VIEW_LANGUAGE };
enum { OP_NONE, OP_SEARCH, OP_ITEM, OP_DOWNLOAD, OP_PREVIEW, OP_COVER, OP_DECODE };
#define QUEUE_MAX 24
#define APP_DIR "sdmc:/3ds/ShieldOSTArchives"
#define OUT_DIR "sdmc:/download audio"

typedef struct { char id[128]; IaTrack track; } QueueEntry;
static QueueEntry queue[QUEUE_MAX];
static unsigned queue_count;
static bool queue_paused, queue_running;
static unsigned queue_selected;
static bool remove_active_after_cancel;
static char confirmed_queue_id[128], confirmed_queue_name[256];
static C3D_RenderTarget *top, *bottom;
static C2D_TextBuf text_buf;
static IaSearchPage search_page;
static IaItem *item;
static int view, parent_view, selected, search_page_num=1, track_page_num=1;
static int search_selection_before_item;
static int operation, confirmation, guide_page, deferred_action;
static char confirmation_text[180];
static IaTrack confirmed_track;
static char confirmed_id[128];
static IaTrack deferred_track;
static char deferred_id[128];
static char deferred_preview_id[128], deferred_preview_name[256];
static bool net_ready, audio_ready, preview_active, from_queue;
static bool preview_cancel_pending;
static size_t preview_next_stream_bytes;
static uint32_t preview_track_duration;
static int preview_track_format;
static char preview_expected_id[128], preview_expected_name[256];
static uint64_t download_expected;
static int theme_choice;
static unsigned preview_seconds=30;
static int options_row;
static bool language_first_run;
static bool language_from_options;
static int language_selection;
static char query[96], notice[160], target_path[512], temp_path[512], meta_path[512], preview_path[512], pending_id[128];
static char download_url[768];
static char download_validator[192];
static uint64_t download_offset;
static char pending_search_query[96];
static int pending_search_page;
static uint64_t notice_until;
static uint64_t preview_request_time;
static uint64_t covers_ready_at;
static bool preview_diag_logged;
static bool sleep_original, sleep_guarded;
static uint32_t navy,panel,raised,accent,white,muted,green,amber;
static Cover covers[4];
static char cover_urls[4][768],cover_paths[4][512],cover_temp[512],cover_target[512];
static bool cover_failed[4];
static int cover_active_slot=-1;

static void theme_set(int t) {
    theme_choice=t;
    if (t) { navy=C2D_Color32(6,6,8,255); panel=C2D_Color32(28,28,30,255);
        raised=C2D_Color32(53,53,56,255); accent=C2D_Color32(242,242,242,255);
        white=C2D_Color32(255,255,255,255); muted=C2D_Color32(170,170,174,255);
        green=C2D_Color32(240,240,240,255); amber=C2D_Color32(190,190,190,255); }
    else { navy=C2D_Color32(3,18,34,255); panel=C2D_Color32(9,35,57,255);
        raised=C2D_Color32(15,53,77,255); accent=C2D_Color32(23,207,232,255);
        white=C2D_Color32(239,249,255,255); muted=C2D_Color32(151,185,199,255);
        green=C2D_Color32(89,220,157,255); amber=C2D_Color32(248,178,81,255); }
}

static void say(const char *s) {
    snprintf(notice,sizeof(notice),"%s",s ? tr(s) : "");
    notice_until=osGetTime()+5500;
}

static void ask_confirmation(int kind,const char *verb,const char *title) {
    confirmation=kind;
    snprintf(confirmation_text,sizeof(confirmation_text),"%s %.95s ?",tr(verb),title ? title : "");
    notice[0]=0;
    notice_until=0;
}

static void update_sleep_guard(void) {
    bool critical=operation==OP_DOWNLOAD || operation==OP_PREVIEW ||
        operation==OP_DECODE || preview_active;
    if (critical==sleep_guarded) return;
    sleep_guarded=critical;
    aptSetSleepAllowed(critical ? false : sleep_original);
}

static void shape(float x,float y,float w,float h,float r,u32 color) {
    if (w<=r*2 || h<=r*2) return;
    C2D_DrawRectSolid(x+r,y,0,w-r*2,h,color);
    C2D_DrawRectSolid(x,y+r,0,w,h-r*2,color);
    C2D_DrawCircleSolid(x+r,y+r,0,r,color);
    C2D_DrawCircleSolid(x+w-r,y+r,0,r,color);
    C2D_DrawCircleSolid(x+r,y+h-r,0,r,color);
    C2D_DrawCircleSolid(x+w-r,y+h-r,0,r,color);
}

static void label_impl(const char *s,float x,float y,float scale,float width,u32 color,
                       bool translate) {
    if (!s || !*s) return;
    char clipped[256]; snprintf(clipped,sizeof(clipped),"%s",translate ? tr(s) : s);
    size_t len=strlen(clipped);
    C2D_Text t; float actual=0,height=0;
    C2D_TextParse(&t,text_buf,clipped); C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t,scale,scale,&actual,&height);
    if (width>0 && actual>width && len>4) {
        size_t fitted=(size_t)((double)len*width/(double)actual);
        if (fitted>len-4) fitted=len-4;
        if (fitted<4) fitted=4;
        len=fitted;
        while (len && ((unsigned char)clipped[len]&0xC0)==0x80) --len;
        memcpy(clipped+len,"...",4);
        C2D_TextParse(&t,text_buf,clipped); C2D_TextOptimize(&t);
        C2D_TextGetDimensions(&t,scale,scale,&actual,&height);
    }
    while (width>0 && actual>width && len>4) {
        if (actual>width*1.25f && len>12) len-=4;
        else len-=2;
        while (len && ((unsigned char)clipped[len]&0xC0)==0x80) --len;
        memcpy(clipped+len,"...",4);
        C2D_TextParse(&t,text_buf,clipped); C2D_TextOptimize(&t);
        C2D_TextGetDimensions(&t,scale,scale,&actual,&height);
    }
    C2D_DrawText(&t,C2D_WithColor,x,y,0,scale,scale,color);
}

static void label(const char *s,float x,float y,float scale,float width,u32 color) {
    label_impl(s,x,y,scale,width,color,true);
}

static void label_raw(const char *s,float x,float y,float scale,float width,u32 color) {
    label_impl(s,x,y,scale,width,color,false);
}

static const IaTrack *selected_track(void) {
    unsigned n=ia_page_first((unsigned)track_page_num)+(unsigned)selected;
    return item && n<item->count ? &item->tracks[n] : NULL;
}

static const IaArchive *selected_archive(void) {
    return selected>=0 && (unsigned)selected<search_page.count ?
        &search_page.archives[selected] : NULL;
}

static void ensure_folders(void) {
    mkdir("sdmc:/3ds",0777); mkdir(APP_DIR,0777);
    mkdir(APP_DIR "/covers",0777); mkdir(OUT_DIR,0777);
}

static FILE *diagnostic_open(void) {
    FILE *log=fopen(APP_DIR "/diagnostic.log","ab+");
    if (!log) return NULL;
    if (fseek(log,0,SEEK_END)!=0 || ftell(log)>65536) {
        fclose(log); return NULL;
    }
    return log;
}

static void log_transfer(int action,const HttpSnapshot *state) {
    if (action!=OP_DOWNLOAD && action!=OP_PREVIEW) return;
    FILE *log=diagnostic_open(); if (!log) return;
    fprintf(log,"NETWORK %s state=%d http=%ld bytes=%llu resumed=%llu expected=%llu "
            "elapsed_ms=%llu dns_us=%llu connect_us=%llu tls_us=%llu first_us=%llu total_us=%llu "
            "sd_write_us=%llu writes=%u connections=%ld error=%s\n",
            action==OP_PREVIEW ? "preview" : "download",(int)state->state,state->status,
            (unsigned long long)state->received,(unsigned long long)state->resumed_from,
            (unsigned long long)state->expected,
            (unsigned long long)state->elapsed_ms,(unsigned long long)state->dns_us,
            (unsigned long long)state->connect_us,
            (unsigned long long)state->tls_us,(unsigned long long)state->first_byte_us,
            (unsigned long long)state->total_us,(unsigned long long)state->sd_write_us,
            state->write_calls,state->new_connections,state->error);
    fclose(log);
}

static void log_preview_audio(const char *stage,bool opened) {
    FILE *log=diagnostic_open(); if (!log) return;
    fprintf(log,"%s opened=%d rate=%u initial_frames=%u peak=%u consumed=%u since_request_ms=%llu error=%s\n",
            stage,opened ? 1 : 0,audio_sample_rate(),audio_initial_frames(),
            audio_initial_peak(),audio_consumed_blocks(),
            (unsigned long long)(osGetTime()-preview_request_time),audio_error());
    fclose(log);
}

static void covers_prepare(void) {
    /* Give foreground actions a chance to start before low-priority artwork. */
    covers_ready_at=osGetTime()+4000;
    ensure_folders();
    for (int i=0;i<4;++i) {
        cover_clear(&covers[i]); cover_failed[i]=false;
        cover_urls[i][0]=cover_paths[i][0]=0;
        const char *id=NULL,*picture=NULL;
        if (view==VIEW_SEARCH && (unsigned)i<search_page.count)
            id=search_page.archives[i].identifier;
        else if (view==VIEW_TRACKS && item) {
            unsigned index=ia_page_first((unsigned)track_page_num)+(unsigned)i;
            if (index<item->count) {
                id=item->archive.identifier;
                picture=item->tracks[index].cover_file;
            }
        }
        if (!id || !ia_build_cover_url(id,picture,cover_urls[i],sizeof(cover_urls[i]))) continue;
        uint32_t hash=2166136261u;
        for (const unsigned char *p=(const unsigned char *)cover_urls[i];*p;++p)
            hash=(hash^*p)*16777619u;
        snprintf(cover_paths[i],sizeof(cover_paths[i]),APP_DIR "/covers/%08lX.img",(unsigned long)hash);
    }
}

static void covers_update(void) {
    if (view!=VIEW_SEARCH && view!=VIEW_TRACKS) return;
    if (view==VIEW_TRACKS && osGetTime()<covers_ready_at) return;
    for (int i=0;i<4;++i) {
        if (!cover_urls[i][0] || cover_failed[i] || covers[i].loaded) continue;
        struct stat info;
        if (stat(cover_paths[i],&info)==0) {
            if (!cover_load(&covers[i],cover_paths[i])) cover_failed[i]=true;
            return;
        }
        if (operation || queue_count || !net_ready) return;
        snprintf(cover_temp,sizeof(cover_temp),"%s.part",cover_paths[i]);
        if (ia_http_start(cover_urls[i],cover_temp,524288)) {
            snprintf(cover_target,sizeof(cover_target),"%s",cover_paths[i]);
            cover_active_slot=i; operation=OP_COVER;
        } else cover_failed[i]=true;
        return;
    }
}

static void save_theme(void) {
    ensure_folders(); FILE *f=fopen(APP_DIR "/theme.dat","wb");
    if (f) { fputc('0'+theme_choice,f); fclose(f); }
}

static void load_theme(void) {
    int t=0; FILE *f=fopen(APP_DIR "/theme.dat","rb");
    if (f) { t=fgetc(f)=='1'; fclose(f); } theme_set(t);
}

static void load_preview_choice(void) {
    FILE *f=fopen(APP_DIR "/preview.dat","rb");
    if (f) { preview_seconds=fgetc(f)=='5' ? 50 : 30; fclose(f); }
}

static void load_language(void) {
    FILE *f=fopen(APP_DIR "/language.dat","rb");
    if (!f) { language_first_run=true; view=VIEW_LANGUAGE; return; }
    int value=fgetc(f), extra=fgetc(f);
    fclose(f);
    if ((value!='F' && value!='E') || extra!=EOF) {
        language_first_run=true; view=VIEW_LANGUAGE; return;
    }
    i18n_set_english(value=='E');
    language_selection=value=='E';
}

static void save_language(void) {
    ensure_folders();
    FILE *f=fopen(APP_DIR "/language.dat","wb");
    if (!f) { say("Langue non enregistree sur SD."); return; }
    bool failed=fputc(language_selection ? 'E' : 'F',f)==EOF;
    if (fclose(f)!=0) failed=true;
    if (failed) { say("Langue non enregistree sur SD."); return; }
    i18n_set_english(language_selection);
    language_first_run=false;
    view=language_from_options ? VIEW_OPTIONS : VIEW_SEARCH;
    language_from_options=false;
    say("Langue enregistree.");
}

static void save_preview_choice(void) {
    ensure_folders();
    FILE *f=fopen(APP_DIR "/preview.dat","wb");
    if (!f) { say("Option de preecoute non enregistree."); return; }
    bool failed=fputc(preview_seconds==50 ? '5' : '3',f)==EOF;
    if (fclose(f)!=0) failed=true;
    if (failed)
        say("Option de preecoute non enregistree.");
}

static bool save_queue(void) {
    ensure_folders();
    FILE *f=fopen(APP_DIR "/queue.tmp","wb");
    if (!f) { say("File d'attente : ecriture SD impossible."); return false; }
    const char magic[8]={'S','O','S','T','Q','0','1',0};
    bool ok=fwrite(magic,1,8,f)==8 && fwrite(&queue_count,sizeof(queue_count),1,f)==1 &&
        fwrite(queue,sizeof(queue[0]),queue_count,f)==queue_count;
    if (fclose(f)!=0) ok=false;
    if (ok) {
        remove(APP_DIR "/queue.bak");
        rename(APP_DIR "/queue.bin",APP_DIR "/queue.bak");
        if (rename(APP_DIR "/queue.tmp",APP_DIR "/queue.bin")!=0) {
            rename(APP_DIR "/queue.bak",APP_DIR "/queue.bin");
            remove(APP_DIR "/queue.tmp"); say("File d'attente : sauvegarde impossible.");
            return false;
        }
    } else { remove(APP_DIR "/queue.tmp"); say("File d'attente : sauvegarde impossible."); return false; }
    return true;
}

static bool load_queue_file(const char *path) {
    FILE *f=fopen(path,"rb"); if (!f) return false;
    char magic[8]; unsigned count=0;
    bool ok=fread(magic,1,8,f)==8 && !memcmp(magic,"SOSTQ01",7) &&
        fread(&count,sizeof(count),1,f)==1 && count<=QUEUE_MAX &&
        fread(queue,sizeof(queue[0]),count,f)==count;
    fclose(f); if (!ok) return false;
    queue_count=0;
    char url[768];
    for (unsigned i=0;i<count;++i) {
        queue[i].id[sizeof(queue[i].id)-1]=0;
        queue[i].track.name[sizeof(queue[i].track.name)-1]=0;
        queue[i].track.title[sizeof(queue[i].track.title)-1]=0;
        if (ia_build_file_url(queue[i].id,queue[i].track.name,url,sizeof(url)) &&
            queue[i].track.format<=IA_AUDIO_M4A)
            queue[queue_count++]=queue[i];
    }
    return true;
}

static void load_queue(void) {
    if (!load_queue_file(APP_DIR "/queue.bin")) {
        if (load_queue_file(APP_DIR "/queue.bak")) say("File d'attente recuperee depuis la sauvegarde.");
    }
}

static bool network_ready(void) {
    if (net_ready) return true;
    net_ready=ia_http_init();
    if (!net_ready) say(ia_http_init_error());
    return net_ready;
}

static void begin_search(void);

static void keyboard(void) {
    if (operation==OP_DOWNLOAD) { say("Attendez ou annulez le transfert."); return; }
    SwkbdState k; char result[sizeof(query)]={0};
    swkbdInit(&k,SWKBD_TYPE_NORMAL,2,sizeof(query)-1);
    swkbdSetHintText(&k,"Rechercher des archives audio");
    swkbdSetInitialText(&k,query);
    swkbdSetButton(&k,SWKBD_BUTTON_RIGHT,"OK",true);
    if (swkbdInputText(&k,result,sizeof(result))==SWKBD_BUTTON_RIGHT) {
        if (strcmp(query,result)) {
            snprintf(query,sizeof(query),"%s",result);
            deferred_action=0;
            if (operation==OP_SEARCH || operation==OP_COVER) ia_http_cancel();
            memset(&search_page,0,sizeof(search_page)); search_page_num=1;
            view=VIEW_SEARCH; selected=0; covers_prepare();
        }
        if (*query) begin_search();
    }
}

static void begin_search(void) {
    if (!*query) { say("Entrez un titre ou un artiste."); return; }
    if (operation==OP_COVER || operation==OP_SEARCH || operation==OP_ITEM) {
        ia_http_cancel(); deferred_action=OP_SEARCH; say("Nouvelle recherche en attente..."); return;
    }
    if (operation) { say("Attendez la fin du transfert ou annulez-le."); return; }
    if (!network_ready()) return;
    char url[768];
    if (!ia_build_search_url(query,(unsigned)search_page_num,url,sizeof(url)) ||
        !ia_http_start(url,NULL,1048576)) { say("Recherche impossible."); return; }
    snprintf(pending_search_query,sizeof(pending_search_query),"%s",query);
    pending_search_page=search_page_num;
    operation=OP_SEARCH; say("Recherche Internet Archive en cours...");
}

static void begin_item(void) {
    const IaArchive *entry=selected_archive();
    if (operation==OP_COVER) { ia_http_cancel(); deferred_action=OP_ITEM; return; }
    if (!entry || operation || !network_ready()) return;
    char url[768];
    if (!ia_build_metadata_url(entry->identifier,url,sizeof(url)) ||
        !ia_http_start(url,NULL,2097152)) { say("Archives indisponibles."); return; }
    snprintf(pending_id,sizeof(pending_id),"%s",entry->identifier);
    operation=OP_ITEM; say("Chargement des pistes...");
}

static uint64_t sd_free_bytes(void) {
    FS_ArchiveResource sd;
    if (R_FAILED(FSUSER_GetSdmcArchiveResource(&sd))) return 0;
    return (uint64_t)sd.freeClusters*sd.clusterSize;
}

static void safe_name(const char *input,char *out,size_t cap) {
    if (!input || !out || cap<2) return;
    const char *base=strrchr(input,'/'); base=base ? base+1 : input;
    size_t length=strlen(base);
    const char *dot=strrchr(base,'.');
    size_t extension=dot && strlen(dot)<=5 ? strlen(dot) : 0;
    size_t body=length-extension;
    size_t copy=body<cap-1-extension ? body : cap-1-extension;
    if (copy<body) while (copy && ((unsigned char)base[copy]&0xC0)==0x80) --copy;
    size_t at=0;
    for (size_t i=0;i<copy;++i) {
        unsigned char c=(unsigned char)base[i];
        out[at++]=(c<32 || c==127 || c=='<' || c=='>' || c==':' ||
            c=='"' || c=='/' || c=='\\' || c=='|' || c=='?' || c=='*') ? '_' : (char)c;
    }
    for (size_t i=body;i<length;++i) {
        unsigned char c=(unsigned char)base[i];
        out[at++]=(c<32 || c==127 || c=='<' || c=='>' || c==':' ||
            c=='"' || c=='/' || c=='\\' || c=='|' || c=='?' || c=='*') ? '_' : (char)c;
    }
    out[at]=0;
    if (!at || !strcmp(out,".") || !strcmp(out,"..")) snprintf(out,cap,"audio.dat");
}

static uint64_t url_hash(const char *url) {
    uint64_t value=1469598103934665603ull;
    for (const unsigned char *p=(const unsigned char *)url;*p;++p)
        value=(value^*p)*1099511628211ull;
    return value;
}

static bool read_download_meta(const char *url,char *validator,size_t cap) {
    FILE *f=fopen(meta_path,"rb");
    if (!f) return false;
    char stored[768];
    bool valid=fgets(stored,sizeof(stored),f)!=NULL;
    bool matched=false;
    if (valid) stored[strcspn(stored,"\r\n")]=0;
    if (valid && !strcmp(stored,url)) {
        matched=true;
        if (!fgets(validator,cap,f)) validator[0]=0;
        validator[strcspn(validator,"\r\n")]=0;
    }
    fclose(f);
    return matched;
}

static bool write_download_meta(const char *url,const char *validator) {
    char pending[sizeof(meta_path)+5], backup[sizeof(meta_path)+5];
    snprintf(pending,sizeof(pending),"%s.tmp",meta_path);
    snprintf(backup,sizeof(backup),"%s.bak",meta_path);
    FILE *f=fopen(pending,"wb");
    if (!f) return false;
    bool good=fprintf(f,"%s\n%s\n",url,validator ? validator : "")>0;
    if (fclose(f)!=0) good=false;
    if (!good) { remove(pending); return false; }
    struct stat existing;
    bool had_old=stat(meta_path,&existing)==0;
    if (had_old) {
        remove(backup);
        if (rename(meta_path,backup)!=0) { remove(pending); return false; }
    }
    if (rename(pending,meta_path)!=0) {
        if (had_old) rename(backup,meta_path);
        remove(pending); return false;
    }
    if (had_old) remove(backup);
    return true;
}

static bool begin_download(const char *id,const IaTrack *track,bool queued) {
    if (operation || !track || !track->downloadable || !network_ready()) return false;
    char url[768],filename[128];
    if (!ia_build_file_url(id,track->name,url,sizeof(url))) { say("Lien audio invalide."); return false; }
    uint64_t free_bytes=sd_free_bytes();
    if (free_bytes<=4u*1024u*1024u) {
        say("Espace libre SD insuffisant ou inconnu."); return false;
    }
    ensure_folders(); safe_name(track->name,filename,sizeof(filename));
    struct stat info;
    bool slot_available=false;
    for (int suffix=0;suffix<1000;++suffix) {
        if (suffix) snprintf(target_path,sizeof(target_path),OUT_DIR "/%d_%s",suffix,filename);
        else snprintf(target_path,sizeof(target_path),OUT_DIR "/%s",filename);
        if (stat(target_path,&info)!=0) { slot_available=true; break; }
    }
    if (!slot_available) { say("Trop de fichiers du meme nom."); return false; }
    unsigned long long hash=(unsigned long long)url_hash(url);
    snprintf(temp_path,sizeof(temp_path),APP_DIR "/download-%016llX.part",hash);
    snprintf(meta_path,sizeof(meta_path),APP_DIR "/download-%016llX.meta",hash);
    char validator[192]={0};
    uint64_t offset=0;
    if (stat(temp_path,&info)==0) {
        if (!read_download_meta(url,validator,sizeof(validator))) {
            say("Reprise impossible : fichier partiel non reconnu."); return false;
        }
        if (info.st_size<0 || (uint64_t)info.st_size>=512u*1024u*1024u) {
            say("Fichier partiel invalide ou trop volumineux."); return false;
        }
        offset=(uint64_t)info.st_size;
        /* Without an ETag or Last-Modified, the old and new bytes cannot be
           proven to belong to the same object. Restart instead of appending. */
        if (!validator[0]) offset=0;
    }
    if (track->bytes && track->bytes>offset &&
        track->bytes-offset>free_bytes-4u*1024u*1024u) {
        say("Espace libre SD insuffisant ou inconnu."); return false;
    }
    uint64_t available=free_bytes-4u*1024u*1024u;
    size_t max_bytes=(size_t)(available<512u*1024u*1024u ? available : 512u*1024u*1024u);
    if (offset>512u*1024u*1024u-max_bytes) max_bytes=512u*1024u*1024u;
    else max_bytes+=offset;
    if (!write_download_meta(url,validator)) {
        say("Impossible d'enregistrer la reprise sur SD."); return false;
    }
    if (!ia_http_start_resume(url,temp_path,max_bytes,offset,validator)) {
        say("Telechargement impossible."); return false;
    }
    snprintf(download_url,sizeof(download_url),"%s",url);
    snprintf(download_validator,sizeof(download_validator),"%s",validator);
    download_offset=offset;
    download_expected=track->bytes;
    from_queue=queued; operation=OP_DOWNLOAD;
    say(offset ? "Reprise du telechargement..." : "Telechargement en cours...");
    return true;
}

static void enqueue(const char *id,const IaTrack *track,bool front) {
    if (!track || !track->downloadable) { say("Cette piste n'est pas telechargeable."); return; }
    if (queue_count>=QUEUE_MAX) { say("File d'attente pleine."); return; }
    for (unsigned i=0;i<queue_count;++i)
        if (!strcmp(queue[i].id,id) && !strcmp(queue[i].track.name,track->name)) {
            say("Deja dans la file d'attente."); return;
        }
    if (front) memmove(queue+1,queue,queue_count*sizeof(queue[0]));
    unsigned at=front ? 0 : queue_count;
    snprintf(queue[at].id,sizeof(queue[at].id),"%s",id);
    queue[at].track=*track; ++queue_count;
    if (!save_queue()) {
        --queue_count;
        if (front) memmove(queue,queue+1,queue_count*sizeof(queue[0]));
        return;
    }
    say("Ajoute a la file.");
}

static void start_preview(void) {
    const IaTrack *track=selected_track();
    if (preview_active) {
        audio_stop(); preview_active=false;
        if (operation==OP_PREVIEW) {
            preview_cancel_pending=true; ia_http_cancel();
        } else remove(preview_path);
        say("Preecoute arretee."); return;
    }
    if (operation==OP_PREVIEW) {
        preview_cancel_pending=true; ia_http_cancel(); say("Arret de la preecoute..."); return;
    }
    if (operation==OP_COVER) {
        if (deferred_action==OP_PREVIEW) { deferred_action=0; say("Preecoute annulee."); }
        else {
            if (!track || !item) return;
            snprintf(deferred_preview_id,sizeof(deferred_preview_id),"%s",item->archive.identifier);
            snprintf(deferred_preview_name,sizeof(deferred_preview_name),"%s",track->name);
            ia_http_cancel(); deferred_action=OP_PREVIEW;
        }
        return;
    }
    if (view!=VIEW_TRACKS || !track || operation) return;
    if (!track->downloadable) {
        say("Piste privee : preecoute indisponible."); return;
    }
    if (!audio_ready || !network_ready()) { say("Audio ou reseau indisponible."); return; }
    char url[768];
    if (!ia_build_file_url(item->archive.identifier,track->name,url,sizeof(url))) {
        say("Lien audio invalide."); return;
    }
    ensure_folders();
    snprintf(preview_path,sizeof(preview_path),APP_DIR "/preview.%s",
             ia_format_name(track->format));
    snprintf(preview_expected_id,sizeof(preview_expected_id),"%s",item->archive.identifier);
    snprintf(preview_expected_name,sizeof(preview_expected_name),"%s",track->name);
    preview_cancel_pending=false;
    preview_next_stream_bytes=128u*1024u;
    preview_request_time=osGetTime();
    preview_track_duration=track->duration_ms;
    preview_track_format=track->format;
    size_t limit=ia_preview_range_bytes(track,preview_seconds);
    FILE *log=diagnostic_open();
    if (log) {
        fprintf(log,"PREVIEW_REQUEST format=%s total=%llu duration_ms=%u requested_ms=%u range=%lu\n",
            ia_format_name(track->format),(unsigned long long)track->bytes,
            (unsigned)track->duration_ms,preview_seconds*1000u,(unsigned long)limit);
        fclose(log);
    }
    if (!ia_http_start_range(url,preview_path,limit)) { say("Preecoute impossible."); return; }
    operation=OP_PREVIEW; say("Preparation de la preecoute...");
}

static bool remove_first_queue(void) {
    if (!queue_count) return true;
    QueueEntry previous=queue[0];
    --queue_count; memmove(queue,queue+1,queue_count*sizeof(queue[0]));
    if (!save_queue()) {
        memmove(queue+1,queue,queue_count*sizeof(queue[0]));
        queue[0]=previous; ++queue_count;
        queue_running=false; queue_paused=true;
        return false;
    }
    if (queue_selected>=queue_count) queue_selected=queue_count ? queue_count-1 : 0;
    if (!queue_count) { queue_running=false; queue_paused=false; }
    return true;
}

static void run_deferred(void) {
    int action=deferred_action; deferred_action=0;
    if (action==OP_SEARCH) begin_search();
    else if (action==OP_ITEM) begin_item();
    else if (action==OP_PREVIEW) {
        const IaTrack *now=selected_track();
        if (item && now && !strcmp(item->archive.identifier,deferred_preview_id) &&
            !strcmp(now->name,deferred_preview_name)) start_preview();
    }
    else if (action==OP_DOWNLOAD) begin_download(deferred_id,&deferred_track,false);
}

static void poll_network(void) {
    if (operation==OP_DECODE) {
        operation=OP_NONE;
        bool opened=audio_open(preview_path,0);
        log_preview_audio("DECODER",opened);
        if (opened) {
            preview_active=true;
            preview_diag_logged=false;
            say("Preecoute : B pour arreter.");
        } else { say(audio_error()); remove(preview_path); }
        if (deferred_action) run_deferred();
        return;
    }
    if (net_ready && operation) {
        HttpSnapshot state=ia_http_snapshot();
        if (state.state==HTTP_BUSY) {
            if (operation==OP_PREVIEW && !preview_cancel_pending && !preview_active &&
                state.received>=preview_next_stream_bytes &&
                (preview_track_format==IA_AUDIO_MP3 || preview_track_format==IA_AUDIO_AAC)) {
                /* A large ID3 tag or AAC header may need more data. Retry only
                   after another 128 KiB, never on every UI frame. */
                preview_next_stream_bytes+=128u*1024u;
                uint64_t before=osGetTime();
                bool opened=audio_open_stream(preview_path,preview_track_duration);
                FILE *log=diagnostic_open();
                if (log) {
                    fprintf(log,"PREVIEW_START bytes=%llu decode_ms=%llu opened=%d\n",
                        (unsigned long long)state.received,
                        (unsigned long long)(osGetTime()-before),opened ? 1 : 0);
                    fclose(log);
                }
                log_preview_audio("STREAM_DECODER",opened);
                if (opened) {
                    preview_active=true;
                    preview_diag_logged=false;
                    say("Preecoute : B pour arreter.");
                }
            }
            return;
        }
        int finished=operation; operation=OP_NONE;
        log_transfer(finished,&state);
        if (state.state==HTTP_ERROR) {
            if (finished!=OP_COVER) say(state.error);
            else if (cover_active_slot>=0 && cover_active_slot<4 &&
                     !strcmp(cover_paths[cover_active_slot],cover_target))
                cover_failed[cover_active_slot]=true;
            if (finished==OP_DOWNLOAD && from_queue) {
                if (remove_active_after_cancel) {
                    remove_active_after_cancel=false; remove_first_queue();
                } else { queue_running=false; queue_paused=true; }
            }
            if (finished==OP_DOWNLOAD) {
                if (state.status==416) {
                    remove(temp_path); remove(meta_path);
                    say("Fichier partiel perime. Reessayez pour repartir de zero.");
                } else if (ia_http_validator()[0] &&
                           (state.status==200 || !download_offset || !download_validator[0] ||
                            !strcmp(download_validator,ia_http_validator())))
                    write_download_meta(download_url,ia_http_validator());
            }
            if (finished==OP_PREVIEW) {
                if (preview_active && !preview_cancel_pending) {
                    audio_set_streaming(false);
                    say("Reseau interrompu : fin de l'extrait recu.");
                } else {
                    if (preview_active) audio_stop();
                    preview_active=false; remove(preview_path);
                }
            }
            ia_http_reset();
            if (deferred_action) run_deferred();
            return;
        }
        if (state.state!=HTTP_DONE) return;
        if (finished==OP_SEARCH || finished==OP_ITEM) {
            size_t bytes=0; char *body=ia_http_take_body(&bytes);
            if (finished==OP_SEARCH) {
                IaSearchPage found;
                if (strcmp(pending_search_query,query) || pending_search_page!=search_page_num) {
                    /* A newer query superseded this completed response. */
                } else if (body && ia_parse_search(body,bytes,(unsigned)search_page_num,&found)) {
                    search_page=found; selected=0; view=VIEW_SEARCH; covers_prepare();
                    say(found.count ? "Archives trouvees." : "Aucun resultat pour cette recherche.");
                } else say("Reponse de recherche invalide.");
            } else {
                IaItem *found=malloc(sizeof(*found));
                if (found && body && ia_parse_item(body,bytes,pending_id,found)) {
                    free(item); item=found; found=NULL;
                    search_selection_before_item=selected; selected=0; track_page_num=1;
                    view=VIEW_TRACKS; covers_prepare();
                    say(item->count ? "Pistes chargees." : "Aucune piste audio dans cette archive.");
                } else say("Metadonnees indisponibles ou trop volumineuses.");
                free(found);
            }
            free(body);
        } else if (finished==OP_DOWNLOAD) {
            remove_active_after_cancel=false;
            if (ia_http_validator()[0])
                write_download_meta(download_url,ia_http_validator());
            if (download_expected && state.received!=download_expected) {
                if (from_queue) { queue_running=false; queue_paused=true; }
                say("Taille incomplete : telechargement a reprendre.");
            } else if (rename(temp_path,target_path)==0) {
                remove(meta_path);
                if (!from_queue || remove_first_queue()) say("Telechargement termine.");
            } else {
                if (from_queue) { queue_running=false; queue_paused=true; }
                say("Enregistrement final sur SD impossible."); }
        } else if (finished==OP_PREVIEW) {
            const IaTrack *selected_now=selected_track();
            if (preview_cancel_pending || view!=VIEW_TRACKS || !item ||
                strcmp(item->archive.identifier,preview_expected_id) ||
                !selected_now || strcmp(selected_now->name,preview_expected_name)) {
                if (preview_active) audio_stop();
                preview_active=false;
                remove(preview_path);
                preview_cancel_pending=false;
                say("Preecoute annulee.");
            } else if (preview_active) audio_set_streaming(false);
            else operation=OP_DECODE;
        } else if (finished==OP_COVER && cover_active_slot>=0 && cover_active_slot<4) {
            int slot=cover_active_slot;
            if (rename(cover_temp,cover_target)==0) {
                if (!strcmp(cover_paths[slot],cover_target) &&
                    !cover_load(&covers[slot],cover_target)) cover_failed[slot]=true;
            } else { remove(cover_temp); if (!strcmp(cover_paths[slot],cover_target)) cover_failed[slot]=true; }
        }
        ia_http_reset();
        if (deferred_action && operation!=OP_DECODE) run_deferred();
    }
    if (!operation && queue_running && !queue_paused && queue_count) {
        if (!begin_download(queue[0].id,&queue[0].track,true)) {
            queue_running=false; queue_paused=true;
        }
    }
    if (!operation) covers_update();
}

static void text_time(uint32_t ms,char out[16]) {
    if (!ms) snprintf(out,16,"--:--");
    else snprintf(out,16,"%02u:%02u",(unsigned)(ms/60000),(unsigned)(ms/1000%60));
}

static void draw_top(void) {
    C2D_SceneBegin(top); C2D_TargetClear(top,navy);
    shape(10,8,380,224,12,panel);
    shape(18,15,364,32,8,raised);
    label("SHIELD",28,18,.72f,130,white);
    label("ONLINE ARCHIVE",148,21,.49f,165,accent);
    label("v0.2.4",337,23,.39f,44,muted);
    if (view==VIEW_TRACKS && item) {
        label_raw(item->archive.title,22,53,.49f,preview_active ? 195 : 350,white);
        if (preview_active) {
            char elapsed[16],limit[16],status[48];
            uint32_t duration=audio_duration_ms();
            if (!duration || duration>preview_seconds*1000u) duration=preview_seconds*1000u;
            text_time(audio_position_ms(),elapsed); text_time(duration,limit);
            snprintf(status,sizeof(status),"%s %s/%s",
                audio_buffering() ? tr("Bufferisation") : tr("Ecoute"),elapsed,limit);
            label_raw(status,226,55,.39f,145,accent);
        }
        if (!item->count) {
            label("Aucune piste compatible",30,95,.57f,335,amber);
            label("Archive vide ou format non pris en charge",30,129,.43f,335,white);
            label("B : revenir aux resultats",30,163,.43f,335,muted);
        }
        for (unsigned i=0;i<4;++i) {
            unsigned index=ia_page_first((unsigned)track_page_num)+i;
            float x=22+(i%2)*182, y=78+(i/2)*67;
            shape(x,y,172,60,8,index<item->count && (int)i==selected ? accent : raised);
            if (index<item->count) {
                shape(x+2,y+2,168,56,7,navy);
                if (covers[i].loaded) cover_draw(&covers[i],x+3,y+3,54);
                else {
                    C2D_DrawCircleSolid(x+24,y+25,0,15,raised);
                    C2D_DrawCircleSolid(x+24,y+25,0,7,accent);
                }
                label_raw(item->tracks[index].title,x+46,y+10,.45f,117,white);
                if (item->tracks[index].downloadable)
                    label(ia_format_name(item->tracks[index].format),x+46,y+34,.39f,109,muted);
                else label("ACCES PRIVE",x+46,y+34,.39f,109,amber);
            }
        }
        if (item->truncated) label("Liste partielle : archive tres grande",24,215,.37f,330,amber);
    } else if (view==VIEW_SEARCH && search_page.count) {
        label("ARCHIVES AUDIO",24,57,.61f,340,white);
        for (unsigned i=0;i<search_page.count;++i) {
            float x=22+(i%2)*182, y=81+(i/2)*64;
            shape(x,y,172,58,8,(int)i==selected ? accent : raised);
            shape(x+2,y+2,168,54,7,navy);
            if (covers[i].loaded) cover_draw(&covers[i],x+3,y+2,54);
            else {
                C2D_DrawCircleSolid(x+24,y+25,0,14,raised);
                C2D_DrawCircleSolid(x+24,y+25,0,6,accent);
            }
            label_raw(search_page.archives[i].title,x+45,y+9,.46f,118,white);
            label_raw(search_page.archives[i].creator,x+45,y+33,.37f,119,muted);
        }
    } else {
        C2D_DrawCircleSolid(94,135,0,64,raised);
        C2D_DrawCircleSolid(94,135,0,54,navy);
        C2D_DrawCircleSolid(94,135,0,44,raised);
        C2D_DrawCircleSolid(94,135,0,36,navy);
        C2D_DrawCircleSolid(94,135,0,23,accent);
        label("Vos archives audio",175,82,.62f,192,white);
        label("Internet Archive",175,112,.50f,195,accent);
        label("Recherchez un titre ou artiste",175,146,.44f,190,muted);
        label("Puis choisissez Audio",175,172,.44f,190,muted);
    }
    if (operation && operation!=OP_COVER) {
        const char *stage=operation==OP_DECODE ? "Decodage audio" :
            operation==OP_DOWNLOAD ? "Telechargement" :
            operation==OP_PREVIEW ? "Preecoute" :
            operation==OP_ITEM ? "Chargement des pistes" : "Recherche";
        stage=tr(stage);
        HttpSnapshot progress=ia_http_snapshot();
        char status[80];
        uint64_t transferred=progress.received>=progress.resumed_from ?
            progress.received-progress.resumed_from : progress.received;
        unsigned speed=progress.elapsed_ms>=1000 ?
            (unsigned)(transferred*1000/(progress.elapsed_ms*1024)) : 0;
        if (operation==OP_DECODE) snprintf(status,sizeof(status),"Decodage audio en cours...");
        else if (operation==OP_PREVIEW && progress.received==0)
            snprintf(status,sizeof(status),"%s : %s (%llu s)",stage,
                i18n_is_english() ? "connecting" : "connexion",
                (unsigned long long)(progress.elapsed_ms/1000));
        else if (progress.expected) {
            unsigned percent=(unsigned)((100.0*(double)progress.received)/
                                        (double)progress.expected);
            if (percent>100) percent=100;
            snprintf(status,sizeof(status),"%s %u%% %u %s/s",stage,percent,speed,
                i18n_is_english() ? "KB" : "Ko");
        } else snprintf(status,sizeof(status),"%s %llu %s %u %s/s",stage,
            (unsigned long long)(progress.received/1024),i18n_is_english() ? "KB" : "Ko",
            speed,i18n_is_english() ? "KB" : "Ko");
        shape(22,207,356,23,6,raised);
        label_raw(status,32,210,.43f,235,accent);
        if (operation==OP_DOWNLOAD || operation==OP_PREVIEW || operation==OP_DECODE)
            label("Veille OFF",286,211,.38f,86,amber);
    } else if (view==VIEW_SEARCH || view==VIEW_TRACKS) {
        unsigned page=view==VIEW_SEARCH ? (unsigned)search_page_num : (unsigned)track_page_num;
        unsigned total=view==VIEW_SEARCH ? ia_page_count(search_page.total) :
            item ? ia_page_count(item->count) : 0;
        char pages[32]; snprintf(pages,sizeof(pages),"%u/%u",total ? page : 0,total);
        shape(22,207,356,23,6,raised);
        label("L <",32,210,.43f,45,white);
        label(pages,179,210,.44f,55,accent);
        label("> R",339,210,.43f,40,white);
    }
    if (!confirmation && !operation && *notice && osGetTime()<notice_until) {
        shape(21,204,358,25,6,raised); label_raw(notice,28,208,.43f,342,white);
    }
}

static void draw_confirmation(void) {
    if (!confirmation) return;
    shape(12,173,296,62,8,navy);
    shape(14,175,292,58,8,raised);
    label_raw(confirmation_text,23,182,.42f,274,white);
    shape(22,205,126,25,5,accent);
    shape(172,205,126,25,5,panel);
    label("A Oui",63,209,.43f,75,navy);
    label("B Non",215,209,.43f,75,white);
}

static void draw_bottom(void) {
    C2D_SceneBegin(bottom); C2D_TargetClear(bottom,navy);
    if (view==VIEW_OPTIONS || view==VIEW_GUIDE || view==VIEW_QUEUE || view==VIEW_LANGUAGE) {
        shape(8,8,304,225,12,panel);
        if (view==VIEW_OPTIONS) {
            label("OPTIONS",19,16,.71f,270,white);
            const char *names[5]={"Theme visuel","Temps de preecoute","Langue",
                "File d'attente","Guide et credits"};
            char theme_value[40], preview_value[20];
            snprintf(theme_value,sizeof(theme_value),"%s",tr(theme_choice ? "Noir / blanc" : "Bleu cyber"));
            snprintf(preview_value,sizeof(preview_value),"%u s",preview_seconds);
            const char *values[5]={theme_value,preview_value,
                i18n_is_english() ? "English" : "Francais",">",">"};
            for (int row=0;row<5;++row) {
                int y=49+row*34;
                shape(17,y,286,29,7,row==options_row ? accent : raised);
                label(names[row],26,y+4,.44f,190,row==options_row ? navy : white);
                label_raw(values[row],221,y+4,.43f,71,row==options_row ? navy : muted);
            }
        } else if (view==VIEW_LANGUAGE) {
            label("Choisissez votre langue",19,18,.56f,282,white);
            label("Choose your language",19,44,.48f,282,accent);
            shape(17,86,286,39,8,language_selection==0 ? accent : raised);
            label("Francais",29,95,.56f,250,language_selection==0 ? navy : white);
            shape(17,140,286,39,8,language_selection==1 ? accent : raised);
            label("English",29,149,.56f,250,language_selection==1 ? navy : white);
            label("A : OK    B : Retour / Back",19,202,.42f,283,muted);
        } else if (view==VIEW_QUEUE) {
            char title[32]; snprintf(title,sizeof(title),"%s  %u/%d",
                i18n_is_english() ? "QUEUE" : "FILE",queue_count,QUEUE_MAX);
            label(title,19,16,.65f,270,white);
            unsigned first=(queue_selected/5)*5;
            for (unsigned i=first;i<queue_count && i<first+5;++i) {
                int y=52+(int)(i-first)*27;
                if (i==queue_selected) shape(16,y-3,288,25,6,raised);
                label_raw(queue[i].track.title,21,y,.45f,216,i==queue_selected ? white : muted);
                label(i==0 && operation==OP_DOWNLOAD && from_queue ? "ACTIF" : "ATTENTE",
                      241,y+2,.42f,59,i==0 ? accent : muted);
            }
            if (!queue_count) label("Aucun telechargement en attente",20,88,.44f,280,muted);
            if (queue_paused && queue_count) label("File en pause. A pour reprendre.",18,190,.39f,286,amber);
            if (operation==OP_DOWNLOAD && from_queue) {
                HttpSnapshot h=ia_http_snapshot();
                if (h.expected) {
                    float progress=275.0f*(float)((double)h.received/(double)h.expected);
                    if (progress>275) progress=275;
                    shape(20,207,275,5,2,raised);
                    if (progress>4) shape(20,207,progress,5,2,accent);
                }
            }
            label(queue_running ? "A Pause   X Retirer   B Retour" :
                  "A Demarrer   X Retirer   B Retour",18,218,.39f,290,muted);
        } else {
            char head[32]; snprintf(head,sizeof(head),"GUIDE %d/4",guide_page);
            label(head,19,15,.69f,270,white);
            const char *lines[4][5]={
                {"La barre du haut ouvre le clavier.","Saisissez un titre ou un artiste.","Validez pour chercher en ligne.","A ouvre une archive puis une piste.","L/R change de page."},
                {"Y lance ou arrete la preecoute.","A propose un transfert direct.","X ajoute sans telecharger.","Demarrez la file dans SELECT.","Les fichiers vont dans download audio."},
                {"SELECT : langue et duree de preecoute.","Veille bloquee pendant un transfert.","Clapet ferme : batterie consommee.","Connexion requise pour chercher.","B revient a la page precedente."},
                {"Idee et audios : Neo Foxy76.","Conception : Shield-77.","Code : aide de Codex / OpenAI.","Audio AAC : FAAD2 / Nero AG.","Projet gratuit, fait sur temps libre."}
            };
            int page=guide_page>=1 && guide_page<=4 ? guide_page-1 : 0;
            for (int i=0;i<5;++i) label(lines[page][i],18,54+i*29,.44f,284,i==0 ? accent : white);
            label("B Precedent       A Suivant",18,214,.40f,287,muted);
        }
        draw_confirmation();
        return;
    }
    shape(9,8,302,36,9,panel);
    if (*query) label_raw(query,17,15,.50f,284,white);
    else label("Touchez pour rechercher...",17,15,.50f,284,muted);
    shape(9,50,200,31,8,accent); label("Audio",79,56,.54f,75,navy);
    shape(217,50,94,31,8,panel); label("Image",239,57,.46f,70,muted);
    unsigned visible=view==VIEW_SEARCH ? search_page.count :
        view==VIEW_TRACKS && item ? item->count-ia_page_first((unsigned)track_page_num) : 0;
    if (visible>4) visible=4;
    for (unsigned i=0;i<visible;++i) {
        float x=215.0f+i*24.0f;
        C2D_DrawCircleSolid(x+9,96,0,10,(int)i==selected ? accent : raised);
        char digit[2]={(char)('1'+i),0};
        label(digit,x+5,90,.39f,12,(int)i==selected ? navy : white);
    }
    if (view==VIEW_SEARCH) {
        char count[64]; snprintf(count,sizeof(count),"Archives : %u",search_page.total);
        label(count,13,88,.48f,180,muted);
        shape(9,112,302,110,9,panel);
        const IaArchive *archive=selected_archive();
        if (archive) {
            label_raw(archive->title,19,120,.60f,282,white);
            label("Archive audio",19,150,.43f,280,accent);
            if (archive->creator[0]) label_raw(archive->creator,19,172,.50f,280,white);
            else label("Artiste non precise",19,172,.50f,280,white);
            if (archive->archive_date[0]) {
                label("Date de l'archive",19,198,.42f,150,muted);
                label_raw(archive->archive_date,173,197,.45f,120,white);
            }
        } else label("Saisissez un titre, puis touchez Audio",19,143,.48f,280,white);
    } else if (view==VIEW_TRACKS && item) {
        char count[64]; snprintf(count,sizeof(count),"%s %u/%u",
            i18n_is_english() ? "Track" : "Piste",
            item->count ? ia_page_first((unsigned)track_page_num)+(unsigned)selected+1 : 0,item->count);
        label(count,13,88,.48f,180,muted);
        shape(9,112,302,110,9,panel);
        const IaTrack *track=selected_track();
        if (track) {
            char size[32],time[16];
            label_raw(track->title,19,119,.57f,282,white);
            if (track->bytes) snprintf(size,sizeof(size),"%llu %s",
                (unsigned long long)((track->bytes+1023)/1024),
                i18n_is_english() ? "KB" : "Ko");
            else snprintf(size,sizeof(size),"Inconnue");
            text_time(track->duration_ms,time);
            const char *names[4]={"Format","Duree","Taille","Date"};
            const char *values[4]={ia_format_name(track->format),time,size,
                track->date[0] ? track->date : "Inconnue"};
            for (int i=0;i<4;++i) {
                float y=145+i*18;
                label(names[i],20,y,.43f,95,muted);
                if (i==3 && track->date[0])
                    label_raw(values[i],118,y,.49f,180,white);
                else label(values[i],118,y,.49f,180,i==0 ? accent : white);
            }
        } else {
            label("Aucune piste compatible",19,137,.50f,281,white);
            label("MP3, WAV, FLAC, AAC, M4A",19,166,.44f,281,muted);
            label("B : retour aux archives",19,194,.44f,281,accent);
        }
    }
    shape(9,226,302,12,5,panel);
    if (operation) {
        HttpSnapshot h=ia_http_snapshot();
        if (h.expected && h.received) {
            float width=302.0f*(float)((double)h.received/(double)h.expected);
            if (width>302) width=302;
            if (width>10) shape(9,226,width,12,5,accent);
        }
    } else if (preview_active) {
        uint32_t duration=audio_duration_ms();
        if (!duration || duration>preview_seconds*1000u) duration=preview_seconds*1000u;
        float width=302.0f*(float)audio_position_ms()/(float)duration;
        if (width>302) width=302;
        if (width>10) shape(9,226,width,12,5,accent);
    }
    draw_confirmation();
}

static void render(void) {
    C2D_TextBufClear(text_buf); C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    draw_top(); draw_bottom(); C3D_FrameEnd(0);
}

static void cancel_preview_for_selection(void) {
    if (view!=VIEW_TRACKS) return;
    if (preview_active) { audio_stop(); preview_active=false; }
    if (operation==OP_PREVIEW) {
        preview_cancel_pending=true; ia_http_cancel();
    } else if (*preview_path) remove(preview_path);
}

static void move_selection(int delta) {
    int count=view==VIEW_SEARCH ? (int)search_page.count :
        item ? (int)(item->count-ia_page_first((unsigned)track_page_num)) : 0;
    if (count>4) count=4;
    if (count<=0) return;
    int before=selected;
    selected+=delta; if (selected<0) selected=0; if (selected>=count) selected=count-1;
    if (selected!=before) cancel_preview_for_selection();
}

static void change_page(int delta) {
    if (view==VIEW_SEARCH) {
        if (operation) return;
        int next=search_page_num+delta;
        if (next<1 || (unsigned)next>ia_page_count(search_page.total)) return;
        search_page_num=next; begin_search();
    } else if (view==VIEW_TRACKS && item) {
        int next=track_page_num+delta;
        if (next<1 || (unsigned)next>ia_page_count(item->count)) return;
        cancel_preview_for_selection();
        track_page_num=next; selected=0; covers_prepare();
    }
}

static void on_back(void) {
    if (confirmation) { confirmation=0; confirmation_text[0]=0; return; }
    if (view==VIEW_LANGUAGE) {
        if (!language_first_run) { view=VIEW_OPTIONS; language_from_options=false; }
        return;
    }
    if (view==VIEW_QUEUE) { view=VIEW_OPTIONS; return; }
    if (operation==OP_DOWNLOAD || operation==OP_PREVIEW) {
        if (operation==OP_PREVIEW) {
            preview_cancel_pending=true;
            if (preview_active) { audio_stop(); preview_active=false; }
        }
        ia_http_cancel(); say("Annulation demandee..."); return;
    }
    if (operation==OP_COVER && deferred_action) {
        deferred_action=0; ia_http_cancel(); say("Action annulee."); return;
    }
    if (preview_active) {
        audio_stop(); preview_active=false; remove(preview_path);
        say("Preecoute arretee."); return;
    }
    if (view==VIEW_GUIDE) {
        if (guide_page>1) --guide_page; else view=VIEW_OPTIONS;
    } else if (view==VIEW_OPTIONS) view=parent_view;
    else if (view==VIEW_TRACKS) {
        view=VIEW_SEARCH; selected=search_selection_before_item; covers_prepare();
    }
    else if (operation) { ia_http_cancel(); say("Annulation demandee..."); }
}

static void handle_action(void) {
    if (confirmation) {
        int wanted=confirmation; confirmation=0; confirmation_text[0]=0;
        if (wanted==3) {
            if (queue_selected>=queue_count ||
                strcmp(queue[queue_selected].id,confirmed_queue_id) ||
                strcmp(queue[queue_selected].track.name,confirmed_queue_name)) {
                say("La file a change. Selectionnez encore la piste."); return;
            }
            if (queue_selected==0 && operation==OP_DOWNLOAD && from_queue) {
                remove_active_after_cancel=true; ia_http_cancel();
                say("Annulation du transfert demandee...");
            } else {
                QueueEntry removed=queue[queue_selected];
                memmove(queue+queue_selected,queue+queue_selected+1,
                        (queue_count-queue_selected-1)*sizeof(queue[0]));
                --queue_count;
                if (!save_queue()) {
                    memmove(queue+queue_selected+1,queue+queue_selected,
                            (queue_count-queue_selected)*sizeof(queue[0]));
                    queue[queue_selected]=removed; ++queue_count;
                    return;
                }
                if (queue_selected>=queue_count) queue_selected=queue_count ? queue_count-1 : 0;
                say("Piste retiree de la file.");
            }
            return;
        }
        if (wanted==1) {
            if (operation==OP_COVER) {
                deferred_track=confirmed_track;
                snprintf(deferred_id,sizeof(deferred_id),"%s",confirmed_id);
                ia_http_cancel(); deferred_action=OP_DOWNLOAD;
                say("Telechargement en attente de la pochette...");
            } else if (operation) say("Attendez la fin de l'action en cours.");
            else begin_download(confirmed_id,&confirmed_track,false);
        } else if (wanted==2) enqueue(confirmed_id,&confirmed_track,false);
        return;
    }
    if (view==VIEW_LANGUAGE) { save_language(); return; }
    if (view==VIEW_GUIDE) {
        if (guide_page<4) ++guide_page; else view=VIEW_OPTIONS;
    } else if (view==VIEW_QUEUE) {
        if (!queue_count) say("La file d'attente est vide.");
        else if (operation==OP_DOWNLOAD && !from_queue)
            say("Un telechargement direct est en cours.");
        else if (queue_running) {
            queue_running=false; queue_paused=true;
            say("File en pause apres la piste en cours.");
        } else {
            queue_running=true; queue_paused=false;
            say("File de telechargements demarree.");
        }
    }
    else if (view==VIEW_OPTIONS) {
        if (options_row==0) { theme_set(!theme_choice); save_theme(); }
        else if (options_row==1) {
            preview_seconds=preview_seconds==30 ? 50 : 30; save_preview_choice();
        } else if (options_row==2) {
            language_selection=i18n_is_english();
            language_from_options=true; view=VIEW_LANGUAGE;
        } else if (options_row==3) view=VIEW_QUEUE;
        else { view=VIEW_GUIDE; guide_page=1; }
    }
    else if (view==VIEW_SEARCH) begin_item();
    else if (view==VIEW_TRACKS && selected_track()) {
        if (!selected_track()->downloadable) {
            say("Piste privee : telechargement indisponible."); return;
        }
        confirmed_track=*selected_track();
        snprintf(confirmed_id,sizeof(confirmed_id),"%s",item->archive.identifier);
        ask_confirmation(1,"Telecharger",confirmed_track.title);
    }
}

static void handle_touch(int x,int y) {
    if (confirmation) {
        if (y>=173 && y<235) {
            if (x<160) handle_action();
            else { confirmation=0; confirmation_text[0]=0; }
        }
        return;
    }
    if (view==VIEW_OPTIONS) {
        if (y>=49 && y<49+5*34) {
            options_row=(y-49)/34;
            handle_action();
        }
        return;
    }
    if (view==VIEW_LANGUAGE) {
        if (y>=86 && y<126) language_selection=0;
        else if (y>=140 && y<180) language_selection=1;
        else if (y>=195) save_language();
        return;
    }
    if (view==VIEW_GUIDE) { if (y>=205) { if (x>=160) handle_action(); else on_back(); } return; }
    if (view==VIEW_QUEUE) {
        if (y>=48 && y<184) {
            unsigned index=(queue_selected/5)*5+(unsigned)((y-48)/27);
            if (index<queue_count) queue_selected=index;
        } else if (y>=213) handle_action();
        return;
    }
    if (y<45) keyboard();
    else if (y>=50 && y<82 && x<209) { search_page_num=1; begin_search(); }
    else if (y>=84 && y<109 && x>=215 && x<311) {
        int row=(x-215)/24;
        unsigned count=view==VIEW_SEARCH ? search_page.count :
            view==VIEW_TRACKS && item ? item->count-ia_page_first((unsigned)track_page_num) : 0;
        if (row>=0 && row<4 && (unsigned)row<count) {
            if (selected!=row) cancel_preview_for_selection();
            selected=row;
        }
    }
    else if (y>=112 && y<222 && x>=9 && x<311 &&
             (view==VIEW_SEARCH || view==VIEW_TRACKS)) handle_action();
}

int main(void) {
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE) || !C2D_Init(4096)) { gfxExit(); return 1; }
    C2D_Prepare(); top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    bottom=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    text_buf=C2D_TextBufNew(8192);
    if (!top || !bottom || !text_buf) { C2D_Fini(); C3D_Fini(); gfxExit(); return 1; }
    for (int i=0;i<4;++i) cover_init(&covers[i]);
    load_theme(); load_preview_choice(); load_queue(); load_language();
    sleep_original=aptIsSleepAllowed();
    audio_ready=audio_init();
    if (!audio_ready) say(audio_error());
    while (aptMainLoop()) {
        hidScanInput(); u32 down=hidKeysDown();
        if (down&KEY_START) { if (operation==OP_DOWNLOAD) say("Attendez ou annulez le transfert."); else break; }
        bool was_playing=audio_playing(); audio_update();
        if (preview_active && !preview_diag_logged && audio_position_ms()>=1000) {
            log_preview_audio("NDSP",audio_playing()); preview_diag_logged=true;
        }
        if (preview_active && audio_position_ms()>=preview_seconds*1000u) {
            audio_stop(); preview_active=false;
            if (operation==OP_PREVIEW) { preview_cancel_pending=true; ia_http_cancel(); }
            else remove(preview_path);
            say("Fin de la preecoute.");
        }
        if (preview_active && was_playing && !audio_playing()) {
            preview_active=false;
            if (operation==OP_PREVIEW) { preview_cancel_pending=true; ia_http_cancel(); }
            else remove(preview_path);
            say("Fin de la preecoute.");
        }
        poll_network();
        if (confirmation) {
            if (down&KEY_B) { confirmation=0; confirmation_text[0]=0; }
            else if (down&KEY_A) handle_action();
        } else if (view==VIEW_LANGUAGE) {
            if (down&(KEY_DUP|KEY_DLEFT)) language_selection=0;
            if (down&(KEY_DDOWN|KEY_DRIGHT)) language_selection=1;
            if (down&KEY_A) save_language();
            if (down&KEY_B) on_back();
        } else if (down&KEY_SELECT) {
            if (view==VIEW_OPTIONS || view==VIEW_GUIDE || view==VIEW_QUEUE) view=parent_view;
            else { parent_view=view; view=VIEW_OPTIONS; options_row=0; }
        } else if (down&KEY_B) on_back();
        else if (down&KEY_A) handle_action();
        else if (view==VIEW_QUEUE) {
            if ((down&KEY_DUP) && queue_selected>0) --queue_selected;
            if ((down&KEY_DDOWN) && queue_selected+1<queue_count) ++queue_selected;
            if ((down&KEY_X) && queue_selected<queue_count) {
                snprintf(confirmed_queue_id,sizeof(confirmed_queue_id),"%s",queue[queue_selected].id);
                snprintf(confirmed_queue_name,sizeof(confirmed_queue_name),"%s",queue[queue_selected].track.name);
                ask_confirmation(3,"Retirer de la file",queue[queue_selected].track.title);
            }
        }
        else if (view==VIEW_OPTIONS) {
            if ((down&KEY_DUP) && options_row>0) --options_row;
            if ((down&KEY_DDOWN) && options_row<4) ++options_row;
        }
        else if (view==VIEW_TRACKS || view==VIEW_SEARCH) {
            if (down&KEY_DUP) move_selection(-2);
            if (down&KEY_DDOWN) move_selection(2);
            if ((down&KEY_DLEFT) && (selected&1)) move_selection(-1);
            if ((down&KEY_DRIGHT) && !(selected&1)) move_selection(1);
            if (down&KEY_L) change_page(-1);
            if (down&KEY_R) change_page(1);
            if ((down&KEY_Y) && view==VIEW_TRACKS) start_preview();
            if ((down&KEY_X) && view==VIEW_TRACKS && selected_track()) {
                confirmed_track=*selected_track();
                snprintf(confirmed_id,sizeof(confirmed_id),"%s",item->archive.identifier);
                ask_confirmation(2,"Ajouter a la file",confirmed_track.title);
            }
        }
        if ((down&KEY_TOUCH) && !(down&(KEY_A|KEY_B|KEY_SELECT))) {
            touchPosition p; hidTouchRead(&p);
            handle_touch(p.px,p.py);
        }
        update_sleep_guard();
        render();
    }
    if (net_ready) ia_http_exit();
    audio_exit(); if (*preview_path) remove(preview_path);
    if (sleep_guarded) aptSetSleepAllowed(sleep_original);
    free(item);
    if (C3D_FrameBegin(0)) C3D_FrameEnd(0);
    for (int i=0;i<4;++i) cover_exit(&covers[i]);
    C2D_TextBufDelete(text_buf);
    C2D_Fini(); C3D_Fini(); gfxExit(); return 0;
}
