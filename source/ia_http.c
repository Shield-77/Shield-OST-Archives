#include "ia_http.h"
#include <3ds.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>

#define SOCKET_BYTES 0x100000

static LightLock lock;
static Thread worker;
static CURL *easy;
static void *socket_buffer;
extern const unsigned char shield_ca_bundle_start[];
extern const unsigned char shield_ca_bundle_end[];
static const char *ca_bundle;
static size_t ca_size;
static HttpSnapshot current;
static bool initialized, running, cancel_requested;
static char request_url[768], request_file[512];
static size_t request_max;
static uint64_t request_started_ms;
static bool request_range;
static bool request_resume;
static uint64_t request_offset;
static char request_validator[192], response_validator[192];
static char *response_body;
static size_t response_length;
static char init_error[120];

typedef struct {
    FILE *file;
    char *body;
    size_t used, cap, limit;
    bool overflow, write_error;
    bool file_request, resume, range_valid;
    long status;
    uint64_t offset;
    char path[512], etag[192], modified[192];
} Sink;

static size_t on_header(char *data,size_t size,size_t count,void *ctx) {
    Sink *sink=ctx;
    if (size && count>SIZE_MAX/size) return 0;
    size_t bytes=size*count;
    if (bytes>=5 && !memcmp(data,"HTTP/",5)) {
        sink->status=0; sink->range_valid=false;
        sink->etag[0]=sink->modified[0]=0;
        sscanf(data,"HTTP/%*s %ld",&sink->status);
        LightLock_Lock(&lock);
        current.status=sink->status;
        if (request_resume && sink->status==200) current.resumed_from=0;
        LightLock_Unlock(&lock);
    } else if (sink->resume) {
        if (bytes>14 && !strncasecmp(data,"Content-Range:",14)) {
            unsigned long long start=0;
            if (sscanf(data+14," bytes %llu-",&start)==1 && start==sink->offset)
                sink->range_valid=true;
        } else if (bytes>5 && !strncasecmp(data,"ETag:",5)) {
            size_t n=bytes-5; const char *value=data+5;
            while (n && (*value==' ' || *value=='\t')) { ++value; --n; }
            while (n && (value[n-1]=='\r' || value[n-1]=='\n' || value[n-1]==' ')) --n;
            if (n && n<sizeof(sink->etag)) { memcpy(sink->etag,value,n); sink->etag[n]=0; }
        } else if (bytes>14 && !strncasecmp(data,"Last-Modified:",14)) {
            size_t n=bytes-14; const char *value=data+14;
            while (n && (*value==' ' || *value=='\t')) { ++value; --n; }
            while (n && (value[n-1]=='\r' || value[n-1]=='\n' || value[n-1]==' ')) --n;
            if (n && n<sizeof(sink->modified)) { memcpy(sink->modified,value,n); sink->modified[n]=0; }
        }
    }
    return bytes;
}

static size_t on_data(char *data, size_t size, size_t count, void *ctx) {
    Sink *sink=ctx;
    if (size && count > SIZE_MAX/size) return 0;
    size_t bytes=size*count;
    if (sink->file_request && !sink->file) {
        if (sink->resume && sink->offset && sink->status==206 && sink->range_valid) {
            const char *validator=sink->etag[0] ? sink->etag : sink->modified;
            if (request_validator[0] && validator[0] &&
                strcmp(request_validator,validator)) { sink->write_error=true; return 0; }
            sink->used=(size_t)sink->offset;
            sink->file=fopen(sink->path,"ab");
        } else if (sink->status==200 || (request_range && sink->status==206)) {
            sink->used=0;
            sink->file=fopen(sink->path,"wb");
        } else { sink->write_error=true; return 0; }
        if (!sink->file) { sink->write_error=true; return 0; }
        setvbuf(sink->file,NULL,_IOFBF,32768);
    }
    if (sink->used>sink->limit || bytes>sink->limit-sink->used) { sink->overflow=true; return 0; }
    if (sink->file_request) {
        uint64_t before=osGetTime();
        if (fwrite(data,1,bytes,sink->file)!=bytes) { sink->write_error=true; return 0; }
        if (request_range && (sink->used+bytes)/65536 != sink->used/65536 &&
            fflush(sink->file)!=0) { sink->write_error=true; return 0; }
        uint64_t after=osGetTime();
        LightLock_Lock(&lock);
        current.sd_write_us+=(after-before)*1000;
        ++current.write_calls;
        LightLock_Unlock(&lock);
    } else {
        if (sink->used+bytes+1>sink->cap) {
            size_t next=sink->cap ? sink->cap*2 : 8192;
            while (next<sink->used+bytes+1) next*=2;
            if (next>sink->limit+1) next=sink->limit+1;
            char *buffer=realloc(sink->body,next);
            if (!buffer) { sink->write_error=true; return 0; }
            sink->body=buffer; sink->cap=next;
        }
        memcpy(sink->body+sink->used,data,bytes);
        sink->body[sink->used+bytes]=0;
    }
    sink->used+=bytes;
    LightLock_Lock(&lock); current.received=sink->used; LightLock_Unlock(&lock);
    return bytes;
}

static int on_progress(void *unused, curl_off_t total, curl_off_t now,
                       curl_off_t upload_total, curl_off_t upload_now) {
    (void)unused; (void)upload_total; (void)upload_now;
    LightLock_Lock(&lock);
    uint64_t base=request_resume && request_offset && current.status==206 ? request_offset : 0;
    uint64_t expected=total>0 ? (uint64_t)total : 0;
    if (request_range && expected>request_max) expected=request_max;
    current.expected=expected ? base+expected : 0;
    if (now>0 && !request_file[0]) current.received=base+(uint64_t)now;
    bool cancelled=cancel_requested;
    LightLock_Unlock(&lock);
    return cancelled ? 1 : 0;
}

static void run_request(void *unused) {
    (void)unused;
    Sink sink={0}; sink.limit=request_max;
    bool file_request=request_file[0]!=0;
    sink.file_request=file_request; sink.resume=request_resume;
    sink.offset=request_offset;
    if (file_request) snprintf(sink.path,sizeof(sink.path),"%s",request_file);
    /* One request is active at a time. Reusing the handle keeps HTTPS and DNS
       state for archive metadata, covers and files. */
    if (!easy) easy=curl_easy_init();
    CURL *curl=easy;
    CURLcode result=CURLE_FAILED_INIT;
    long status=0;
    if (curl) {
        curl_easy_reset(curl);
        struct curl_blob ca={.data=(void *)ca_bundle,.len=ca_size,.flags=CURL_BLOB_NOCOPY};
        bool configured =
            curl_easy_setopt(curl,CURLOPT_URL,request_url)==CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_USERAGENT,"ShieldOnlineArchive/0.2.4") == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https") == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https") == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_CAINFO_BLOB,&ca) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_MAXREDIRS,4L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,256L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,30L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,on_progress) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,on_data) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_WRITEDATA,&sink) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,on_header) == CURLE_OK &&
            curl_easy_setopt(curl,CURLOPT_HEADERDATA,&sink) == CURLE_OK;
        if (configured) {
            configured=curl_easy_setopt(curl,CURLOPT_TIMEOUT,file_request ? 0L : 25L)==CURLE_OK;
            if (configured && request_resume && request_offset) {
                char range[48]; snprintf(range,sizeof(range),"%llu-",(unsigned long long)request_offset);
                configured=curl_easy_setopt(curl,CURLOPT_RANGE,range)==CURLE_OK;
                if (configured && request_validator[0]) {
                    struct curl_slist *headers=NULL;
                    char line[224]; snprintf(line,sizeof(line),"If-Range: %s",request_validator);
                    headers=curl_slist_append(headers,line);
                    configured=headers && curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers)==CURLE_OK;
                    if (configured) result=curl_easy_perform(curl);
                    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,NULL);
                    curl_slist_free_all(headers);
                } else if (configured) result=curl_easy_perform(curl);
            } else if (configured && request_range) {
                char range[48];
                snprintf(range,sizeof(range),"0-%lu",(unsigned long)(request_max-1));
                configured=curl_easy_setopt(curl,CURLOPT_RANGE,range)==CURLE_OK;
                if (configured) result=curl_easy_perform(curl);
            } else if (configured) result=curl_easy_perform(curl);
            curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
            curl_off_t timing=0;
            LightLock_Lock(&lock);
#define COPY_TIME(field, info) do { \
    if (curl_easy_getinfo(curl,info,&timing)==CURLE_OK && timing>=0) \
        current.field=(uint64_t)timing; \
} while (0)
            COPY_TIME(dns_us,CURLINFO_NAMELOOKUP_TIME_T);
            COPY_TIME(connect_us,CURLINFO_CONNECT_TIME_T);
            COPY_TIME(tls_us,CURLINFO_APPCONNECT_TIME_T);
            COPY_TIME(first_byte_us,CURLINFO_STARTTRANSFER_TIME_T);
            COPY_TIME(total_us,CURLINFO_TOTAL_TIME_T);
#undef COPY_TIME
            curl_easy_getinfo(curl,CURLINFO_NUM_CONNECTS,&current.new_connections);
            LightLock_Unlock(&lock);
        }
    }
    if (sink.file) {
        uint64_t before=osGetTime();
        if (fclose(sink.file)!=0) sink.write_error=true;
        uint64_t after=osGetTime();
        LightLock_Lock(&lock);
        current.sd_write_us+=(after-before)*1000;
        LightLock_Unlock(&lock);
    }
    bool capped_preview=request_range && status==200 && sink.overflow &&
        result==CURLE_WRITE_ERROR && sink.used>=256u*1024u;
    bool success=(result==CURLE_OK || capped_preview) &&
        (status==200 || ((request_range || request_resume) && status==206)) &&
        (!sink.overflow || capped_preview) && !sink.write_error;
    if (request_resume && request_offset && status==206 && !sink.range_valid) success=false;
    if (file_request && !sink.file) success=false;
    LightLock_Lock(&lock);
    current.status=status;
    current.received=sink.used;
    snprintf(response_validator,sizeof(response_validator),"%s",sink.etag[0] ? sink.etag : sink.modified);
    current.elapsed_ms=osGetTime()-request_started_ms;
    if (success) {
        response_body=sink.body; response_length=sink.used; sink.body=NULL;
        current.state=HTTP_DONE;
    } else {
        current.state=HTTP_ERROR;
        if (cancel_requested) snprintf(current.error,sizeof(current.error),"Operation annulee.");
        else if (sink.overflow) snprintf(current.error,sizeof(current.error),"Fichier trop volumineux.");
        else if (sink.write_error) snprintf(current.error,sizeof(current.error),"Ecriture SD ou memoire impossible.");
        else if (status==401 || status==403)
            snprintf(current.error,sizeof(current.error),"Acces refuse (%ld) : archive privee ou indisponible.",status);
        else if (status==404)
            snprintf(current.error,sizeof(current.error),"Archive ou piste introuvable (404).");
        else if (status!=200 && status>0) snprintf(current.error,sizeof(current.error),"Serveur HTTP %ld.",status);
        else snprintf(current.error,sizeof(current.error),"Reseau : %s",curl_easy_strerror(result));
    }
    running=false;
    LightLock_Unlock(&lock);
    if (file_request && !success && !request_resume) remove(request_file);
    free(sink.body);
}

bool ia_http_init(void) {
    LightLock_Init(&lock);
    if (initialized) return true;
    ca_bundle=(const char *)shield_ca_bundle_start;
    ca_size=(size_t)(shield_ca_bundle_end-shield_ca_bundle_start);
    if (!ca_size || ca_size>400000) {
        snprintf(init_error,sizeof(init_error),"Certificats HTTPS absents."); return false;
    }
    socket_buffer=memalign(0x1000,SOCKET_BYTES);
    if (!socket_buffer || socInit(socket_buffer,SOCKET_BYTES)!=0) {
        free(socket_buffer); socket_buffer=NULL;
        snprintf(init_error,sizeof(init_error),"Wi-Fi ou memoire reseau indisponible."); return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) {
        socExit(); free(socket_buffer); socket_buffer=NULL;
        snprintf(init_error,sizeof(init_error),"HTTPS indisponible."); return false;
    }
    initialized=true; current.state=HTTP_IDLE;
    return true;
}

static bool start_impl(const char *url, const char *file_path, size_t max_bytes,
                       bool range,bool resume,uint64_t offset,const char *validator) {
    if (!initialized || !url || strncmp(url,"https://",8) || strlen(url)>=sizeof(request_url) ||
        (file_path && strlen(file_path)>=sizeof(request_file)) || !max_bytes) return false;
    ia_http_reset();
    LightLock_Lock(&lock);
    if (running) { LightLock_Unlock(&lock); return false; }
    snprintf(request_url,sizeof(request_url),"%s",url);
    snprintf(request_file,sizeof(request_file),"%s",file_path ? file_path : "");
    request_max=max_bytes; request_range=range; request_resume=resume;
    request_offset=offset; cancel_requested=false;
    snprintf(request_validator,sizeof(request_validator),"%s",validator ? validator : "");
    response_validator[0]=0;
    memset(&current,0,sizeof(current)); current.state=HTTP_BUSY;
    current.resumed_from=offset;
    request_started_ms=osGetTime();
    running=true;
    LightLock_Unlock(&lock);
    /* TLS and redirects can require considerably more stack than the callback itself. */
    worker=threadCreate(run_request,NULL,0x20000,0x30,-2,false);
    if (!worker) {
        LightLock_Lock(&lock);
        running=false; current.state=HTTP_ERROR;
        snprintf(current.error,sizeof(current.error),"Tache reseau impossible.");
        LightLock_Unlock(&lock);
        return false;
    }
    return true;
}

bool ia_http_start(const char *url, const char *file_path, size_t max_bytes) {
    return start_impl(url,file_path,max_bytes,false,false,0,NULL);
}

bool ia_http_start_range(const char *url, const char *file_path, size_t max_bytes) {
    return file_path && start_impl(url,file_path,max_bytes,true,false,0,NULL);
}

bool ia_http_start_resume(const char *url,const char *file_path,size_t max_bytes,
                          uint64_t offset,const char *if_range) {
    return file_path && offset<=max_bytes &&
        start_impl(url,file_path,max_bytes,false,true,offset,if_range);
}

const char *ia_http_validator(void) { return response_validator; }

HttpSnapshot ia_http_snapshot(void) {
    LightLock_Lock(&lock);
    HttpSnapshot copy=current;
    if (copy.state==HTTP_BUSY) copy.elapsed_ms=osGetTime()-request_started_ms;
    LightLock_Unlock(&lock);
    return copy;
}

char *ia_http_take_body(size_t *length) {
    LightLock_Lock(&lock);
    char *body=NULL;
    if (current.state==HTTP_DONE && !request_file[0]) {
        body=response_body; response_body=NULL;
        if (length) *length=response_length;
    }
    LightLock_Unlock(&lock);
    return body;
}

void ia_http_reset(void) {
    LightLock_Lock(&lock); bool can_join=worker && !running; LightLock_Unlock(&lock);
    if (can_join) { threadJoin(worker,UINT64_MAX); threadFree(worker); worker=NULL; }
    if (can_join || !worker) {
        free(response_body); response_body=NULL; response_length=0;
        LightLock_Lock(&lock); if (!running) current.state=HTTP_IDLE; LightLock_Unlock(&lock);
    }
}

void ia_http_cancel(void) {
    LightLock_Lock(&lock); cancel_requested=true; LightLock_Unlock(&lock);
}

void ia_http_exit(void) {
    if (!initialized) return;
    ia_http_cancel();
    if (worker) { threadJoin(worker,UINT64_MAX); threadFree(worker); worker=NULL; }
    ia_http_reset();
    if (easy) { curl_easy_cleanup(easy); easy=NULL; }
    curl_global_cleanup(); socExit();
    free(socket_buffer); socket_buffer=NULL;
    initialized=false;
}

const char *ia_http_init_error(void) { return init_error; }
