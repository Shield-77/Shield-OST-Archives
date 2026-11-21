#include "catalog.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static OstEntry items[OST_MAX_ITEMS];
static int item_count;
#ifdef OST_LOCAL_LAB
static const char *root = "sdmc:/SHIELD/Music/OST/";

static void copy(char *to, size_t cap, const char *from) {
    if (!cap) return;
    snprintf(to, cap, "%s", from ? from : "");
}

static bool mp3_name(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name+n-4, ".mp3");
}

static void add_file(const char *path, const char *name, const char *collection,
                     OstPlatform platform, uint32_t bytes) {
    if (item_count == OST_MAX_ITEMS) return;
    OstEntry *item = &items[item_count++];
    memset(item, 0, sizeof(*item));
    snprintf(item->id, sizeof(item->id), "local-%03d", item_count);
    copy(item->path, sizeof(item->path), path);
    copy(item->title, sizeof(item->title), name);
    size_t len = strlen(item->title);
    if (len >= 4) item->title[len-4] = 0;
    copy(item->collection, sizeof(item->collection), collection);
    item->platform = platform;
    item->bytes = bytes;
    item->available = true;
}

static void scan_folder(const char *folder, const char *collection,
                        OstPlatform platform, int depth) {
    DIR *dir = opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while (item_count < OST_MAX_ITEMS && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || strchr(entry->d_name, '/') ||
            strchr(entry->d_name, '\\')) continue;
        char path[OST_PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s%s", folder, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && depth == 0) {
            if ((size_t)n + 1 >= sizeof(path)) continue;
            strcat(path, "/");
            scan_folder(path, entry->d_name, platform, 1);
        } else if (S_ISREG(st.st_mode) && mp3_name(entry->d_name)) {
            add_file(path, entry->d_name, collection, platform,
                     st.st_size > 0 && (uint64_t)st.st_size < UINT32_MAX ?
                     (uint32_t)st.st_size : 0);
        }
    }
    closedir(dir);
}

static int compare_items(const void *left, const void *right) {
    const OstEntry *a=left, *b=right;
    if (a->platform != b->platform) return (int)a->platform-(int)b->platform;
    return strcasecmp(a->title,b->title);
}

/* The bundled catalogue is clearly marked as examples. It never claims a
   remote connection or offers a file that the user does not have. */
static void demo_entry(const char *id, const char *title, OstPlatform platform,
                       const char *filename) {
    if (item_count == OST_MAX_ITEMS) return;
    OstEntry *item = &items[item_count++];
    memset(item, 0, sizeof(*item));
    copy(item->id, sizeof(item->id), id);
    copy(item->title, sizeof(item->title), title);
    copy(item->collection, sizeof(item->collection), "Catalogue de demonstration");
    snprintf(item->path, sizeof(item->path), "%s%s/%s", root,
             platform == OST_3DS ? "3DS" : "WiiU", filename);
    item->platform = platform;
    item->demo = true;
    struct stat st;
    item->available = stat(item->path, &st) == 0 && S_ISREG(st.st_mode);
    item->bytes = item->available && st.st_size > 0 &&
                  (uint64_t)st.st_size < UINT32_MAX ? (uint32_t)st.st_size : 0;
}
#endif

void catalog_reload(void) {
    item_count = 0;
#ifdef OST_LOCAL_LAB
    scan_folder("sdmc:/SHIELD/Music/OST/3DS/", "3DS", OST_3DS, 0);
    scan_folder("sdmc:/SHIELD/Music/OST/WiiU/", "Wii U", OST_WIIU, 0);
    qsort(items,item_count,sizeof(items[0]),compare_items);
    if (!item_count) {
        demo_entry("demo-3ds", "Exemple 3DS - MP3 local", OST_3DS, "demo-3ds.mp3");
        demo_entry("demo-wiiu", "Exemple Wii U - MP3 local", OST_WIIU, "demo-wiiu.mp3");
    }
#endif
}

int catalog_count(void) { return item_count; }
const OstEntry *catalog_get(int index) {
    return index >= 0 && index < item_count ? &items[index] : NULL;
}

static size_t fold(const char *src, char *dst, size_t cap) {
    size_t out = 0;
    for (size_t i = 0; src[i] && out + 1 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0xc3 && src[i+1]) {
            unsigned char v = (unsigned char)src[++i];
            if ((v >= 0x80 && v <= 0x86) || (v >= 0xa0 && v <= 0xa6)) c='a';
            else if (v == 0x87 || v == 0xa7) c='c';
            else if ((v >= 0x88 && v <= 0x8b) || (v >= 0xa8 && v <= 0xab)) c='e';
            else if ((v >= 0x8c && v <= 0x8f) || (v >= 0xac && v <= 0xaf)) c='i';
            else if (v == 0x91 || v == 0xb1) c='n';
            else if ((v >= 0x92 && v <= 0x96) || (v >= 0xb2 && v <= 0xb6)) c='o';
            else if ((v >= 0x99 && v <= 0x9c) || (v >= 0xb9 && v <= 0xbc)) c='u';
            else if (v == 0x9d || v == 0xbd || v == 0xbf) c='y';
            else continue;
        } else if (c >= 128) continue;
        dst[out++] = (char)tolower(c);
    }
    dst[out] = 0;
    return out;
}

bool catalog_search_match(const char *text, const char *query) {
    char a[256], b[128];
    fold(text, a, sizeof(a));
    fold(query, b, sizeof(b));
    return strstr(a, b) != NULL;
}

int catalog_filter(int *matches, int capacity, int platform, const char *query) {
    int count = 0;
    bool searching = query && *query;
    for (int i = 0; i < item_count && count < capacity; ++i) {
        const OstEntry *item = &items[i];
        if (!searching && (int)item->platform != platform) continue;
        if (searching && !catalog_search_match(item->title, query) &&
            !catalog_search_match(item->collection, query)) continue;
        matches[count++] = i;
    }
    return count;
}
