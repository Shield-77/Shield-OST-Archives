#include "ia_model.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if (argc!=3) return 2;
    FILE *f=fopen(argv[2],"rb"); if (!f) return 2;
    if (fseek(f,0,SEEK_END)!=0) return 2;
    long size=ftell(f);
    if (size<1 || size>2097152 || fseek(f,0,SEEK_SET)!=0) return 2;
    char *json=malloc((size_t)size); if (!json) return 2;
    if (fread(json,1,(size_t)size,f)!=(size_t)size) return 2;
    fclose(f);
    int result=0;
    if (argv[1][0]=='s') {
        IaSearchPage page;
        if (!ia_parse_search(json,(size_t)size,1,&page)) result=1;
        else printf("search: total=%u shown=%u first=%s\n",page.total,page.count,
                    page.count?page.archives[0].identifier:"none");
    } else {
        IaItem *item=malloc(sizeof(*item)); if (!item) return 2;
        if (!ia_parse_item(json,(size_t)size,argv[1],item)) result=1;
        else printf("item: audio=%u title=%s first=%s cover=%s\n",item->count,
                    item->archive.title,item->count?item->tracks[0].name:"none",
                    item->count?item->tracks[0].cover_file:"none");
        free(item);
    }
    free(json); return result;
}
