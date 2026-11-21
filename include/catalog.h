#ifndef SHIELD_OST_CATALOG_H
#define SHIELD_OST_CATALOG_H
#include <stdbool.h>
#include <stdint.h>
#define OST_MAX_ITEMS 256
#define OST_PATH_MAX 512
typedef enum { OST_3DS, OST_WIIU } OstPlatform;
typedef struct {
    char id[32];
    char title[128];
    char collection[96];
    char path[OST_PATH_MAX];
    char archive_id[80];
    OstPlatform platform;
    uint32_t duration_ms;
    uint32_t bytes;
    bool available;
    bool demo;
} OstEntry;
int catalog_count(void);
const OstEntry *catalog_get(int index);
void catalog_reload(void);
int catalog_filter(int *matches, int capacity, int platform, const char *query);
bool catalog_search_match(const char *text, const char *query);
#endif
