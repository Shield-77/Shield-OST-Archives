#include "catalog.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

int main(void) {
    CHECK(catalog_search_match("Mélodie de l'été", "melodie"));
    CHECK(catalog_search_match("L'ÉTOILE", "etoile"));
    CHECK(catalog_search_match("Archives 3DS", "3ds"));
    CHECK(!catalog_search_match("Archive Wii U", "3ds"));
    catalog_reload();
#ifdef OST_LOCAL_LAB
    CHECK(catalog_count() == 2);
    int matches[OST_MAX_ITEMS];
    CHECK(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"") == 1);
    CHECK(catalog_get(matches[0])->platform == OST_3DS);
    CHECK(catalog_filter(matches,OST_MAX_ITEMS,OST_WIIU,"") == 1);
    CHECK(catalog_get(matches[0])->platform == OST_WIIU);
    CHECK(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"exemple") == 2);
#else
    CHECK(catalog_count() == 0);
    int matches[OST_MAX_ITEMS];
    CHECK(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"") == 0);
#endif
    puts("catalogue: recherche et bibliotheques OK");
    return 0;
}
