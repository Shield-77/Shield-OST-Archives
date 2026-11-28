#ifndef SHIELD_ONLINE_COVER_H
#define SHIELD_ONLINE_COVER_H
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

typedef struct {
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    bool initialized;
    bool loaded;
} Cover;

bool cover_init(Cover *cover);
bool cover_load(Cover *cover,const char *path);
void cover_draw(Cover *cover,float x,float y,float size);
void cover_clear(Cover *cover);
void cover_exit(Cover *cover);

#endif
