#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#include "stb_image.h"
#include "cover.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tile_index(unsigned x,unsigned y) {
    unsigned morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
    return (((y>>3)*8+(x>>3))*64)+morton;
}

bool cover_init(Cover *cover) {
    if (!cover || !C3D_TexInit(&cover->tex,64,64,GPU_RGB565)) return false;
    cover->sub=(Tex3DS_SubTexture){64,64,0,1,1,0};
    C3D_TexSetFilter(&cover->tex,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&cover->tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    cover->initialized=true; cover->loaded=false; return true;
}

bool cover_load(Cover *cover,const char *path) {
    if (!cover || !cover->initialized || !path) return false;
    FILE *file=fopen(path,"rb"); if (!file) return false;
    if (fseek(file,0,SEEK_END)!=0) { fclose(file); return false; }
    long size=ftell(file);
    if (size<16 || size>524288 || fseek(file,0,SEEK_SET)!=0) { fclose(file); return false; }
    unsigned char *bytes=malloc((size_t)size);
    if (!bytes) { fclose(file); return false; }
    bool read_ok=fread(bytes,1,(size_t)size,file)==(size_t)size;
    fclose(file);
    int width=0,height=0,channels=0;
    if (!read_ok || !stbi_info_from_memory(bytes,(int)size,&width,&height,&channels) ||
        width<16 || height<16 || width>1024 || height>1024 ||
        (uint64_t)width*height>1048576) { free(bytes); return false; }
    unsigned char *rgb=stbi_load_from_memory(bytes,(int)size,&width,&height,&channels,3);
    free(bytes); if (!rgb) return false;
    unsigned crop=width<height ? (unsigned)width : (unsigned)height;
    unsigned start_x=((unsigned)width-crop)/2,start_y=((unsigned)height-crop)/2;
    uint16_t *pixels=malloc(64*64*sizeof(uint16_t));
    if (!pixels) { stbi_image_free(rgb); return false; }
    for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
        unsigned sx=start_x+(x*crop)/64,sy=start_y+(y*crop)/64;
        if (sx>=(unsigned)width) sx=(unsigned)width-1;
        if (sy>=(unsigned)height) sy=(unsigned)height-1;
        const unsigned char *p=rgb+((size_t)sy*width+sx)*3;
        pixels[tile_index(x,y)]=(uint16_t)(((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3));
    }
    stbi_image_free(rgb);
    C3D_TexUpload(&cover->tex,pixels); C3D_TexFlush(&cover->tex);
    free(pixels); cover->loaded=true; return true;
}

void cover_draw(Cover *cover,float x,float y,float size) {
    if (!cover || !cover->loaded) return;
    C2D_Image image={&cover->tex,&cover->sub};
    C2D_DrawImageAt(image,x,y,0,NULL,size/64.0f,size/64.0f);
}

void cover_clear(Cover *cover) { if (cover) cover->loaded=false; }
void cover_exit(Cover *cover) {
    if (!cover || !cover->initialized) return;
    C3D_TexDelete(&cover->tex); cover->initialized=false; cover->loaded=false;
}
