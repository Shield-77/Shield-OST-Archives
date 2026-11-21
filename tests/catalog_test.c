#include "catalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(catalog_search_match("Mélodie de l'été", "melodie"));
    assert(catalog_search_match("L'ÉTOILE", "etoile"));
    assert(catalog_search_match("Archives 3DS", "3ds"));
    assert(!catalog_search_match("Archive Wii U", "3ds"));
    catalog_reload();
#ifdef OST_LOCAL_LAB
    assert(catalog_count() == 2);
    int matches[OST_MAX_ITEMS];
    assert(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"") == 1);
    assert(catalog_get(matches[0])->platform == OST_3DS);
    assert(catalog_filter(matches,OST_MAX_ITEMS,OST_WIIU,"") == 1);
    assert(catalog_get(matches[0])->platform == OST_WIIU);
    assert(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"exemple") == 2);
#else
    assert(catalog_count() == 0);
    int matches[OST_MAX_ITEMS];
    assert(catalog_filter(matches,OST_MAX_ITEMS,OST_3DS,"") == 0);
#endif
    puts("catalogue: recherche et bibliotheques OK");
    return 0;
}
