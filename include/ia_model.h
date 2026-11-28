#ifndef SHIELD_ONLINE_IA_MODEL_H
#define SHIELD_ONLINE_IA_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IA_PAGE_ITEMS 4
#define IA_MAX_TRACKS 768

typedef enum {
    IA_AUDIO_MP3,
    IA_AUDIO_FLAC,
    IA_AUDIO_WAV,
    IA_AUDIO_AAC,
    IA_AUDIO_M4A
} IaAudioFormat;

typedef struct {
    char identifier[128];
    char title[160];
    char creator[96];
    char archive_date[32];
} IaArchive;

typedef struct {
    char name[256];
    char title[160];
    char date[32];
    char cover_file[256];
    IaAudioFormat format;
    uint64_t bytes;
    uint32_t duration_ms;
    bool downloadable;
} IaTrack;

typedef struct {
    IaArchive archives[IA_PAGE_ITEMS];
    unsigned count;
    unsigned total;
    unsigned page;
} IaSearchPage;

typedef struct {
    IaArchive archive;
    IaTrack tracks[IA_MAX_TRACKS];
    unsigned count;
    unsigned total_audio_files;
    bool truncated;
} IaItem;

bool ia_parse_search(const char *json, size_t size, unsigned page, IaSearchPage *out);
bool ia_parse_item(const char *json, size_t size, const char *identifier, IaItem *out);
bool ia_build_search_url(const char *query, unsigned page, char *out, size_t capacity);
bool ia_build_metadata_url(const char *identifier, char *out, size_t capacity);
bool ia_build_file_url(const char *identifier, const char *filename,
                       char *out, size_t capacity);
bool ia_build_cover_url(const char *identifier, const char *filename,
                        char *out, size_t capacity);
unsigned ia_page_count(unsigned total);
unsigned ia_page_first(unsigned page);
const char *ia_format_name(IaAudioFormat format);
size_t ia_preview_range_bytes(const IaTrack *track, unsigned seconds);

#endif
