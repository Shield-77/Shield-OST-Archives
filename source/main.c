#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "audio.h"
#include "catalog.h"

#define TOP_W 400
#define BOTTOM_W 320
#define LIST_ROWS 5
static C3D_RenderTarget *top, *bottom;
static C2D_TextBuf text_buf;
static int matches[OST_MAX_ITEMS], result_count, selected, scroll_at, platform;
static int playing_index = -1;
static char query[96], message[128];
static uint64_t message_until;
static uint32_t navy, panel, raised, cyan, white, muted, amber, green;
static bool options_open;
static int theme_choice;
static int guide_page;

static void set_theme(int choice) {
    theme_choice=choice;
    if (choice == 1) {
        navy=C2D_Color32(7,7,7,255); panel=C2D_Color32(27,27,27,255);
        raised=C2D_Color32(55,55,55,255); cyan=C2D_Color32(246,246,246,255);
        white=C2D_Color32(255,255,255,255); muted=C2D_Color32(174,174,174,255);
        amber=C2D_Color32(190,190,190,255); green=C2D_Color32(230,230,230,255);
    } else {
        navy=C2D_Color32(3,18,34,255); panel=C2D_Color32(9,35,57,255);
        raised=C2D_Color32(15,53,77,255); cyan=C2D_Color32(23,207,232,255);
        white=C2D_Color32(239,249,255,255); muted=C2D_Color32(151,185,199,255);
        amber=C2D_Color32(248,178,81,255); green=C2D_Color32(89,220,157,255);
    }
}

static void save_theme(void) {
    mkdir("sdmc:/3ds",0777);
    mkdir("sdmc:/3ds/ShieldOSTArchives",0777);
    FILE *f=fopen("sdmc:/3ds/ShieldOSTArchives/theme.dat","wb");
    if (f) { fputc('0'+theme_choice,f); fclose(f); }
}

static void load_theme(void) {
    int theme=0;
    FILE *f=fopen("sdmc:/3ds/ShieldOSTArchives/theme.dat","rb");
    if (f) { theme=fgetc(f)=='1' ? 1 : 0; fclose(f); }
    set_theme(theme);
}

static void say(const char *text) {
    snprintf(message, sizeof(message), "%s", text);
    message_until = osGetTime() + 5000;
}

static void round_rect(float x, float y, float w, float h, float r, uint32_t color) {
    if (w <= 2*r || h <= 2*r) return;
    C2D_DrawRectSolid(x+r,y,0,w-2*r,h,color);
    C2D_DrawRectSolid(x,y+r,0,w,h-2*r,color);
    C2D_DrawCircleSolid(x+r,y+r,0,r,color);
    C2D_DrawCircleSolid(x+w-r,y+r,0,r,color);
    C2D_DrawCircleSolid(x+r,y+h-r,0,r,color);
    C2D_DrawCircleSolid(x+w-r,y+h-r,0,r,color);
}

static void label(const char *value, float x, float y, float scale,
                  float max_width, uint32_t color) {
    if (!value || !*value) return;
    char clipped[192];
    size_t length=strlen(value);
    if (length>=sizeof(clipped)) length=sizeof(clipped)-1;
    memcpy(clipped,value,length); clipped[length]=0;
    C2D_Text t;
    C2D_TextParse(&t, text_buf, clipped);
    C2D_TextOptimize(&t);
    float width=0, height=0;
    C2D_TextGetDimensions(&t, scale, scale, &width, &height);
    if (max_width > 0 && width > max_width) {
        size_t keep=(size_t)(length*max_width/width);
        if (length<=3) keep=0;
        else if (keep>length-3) keep=length-3;
        while (keep && ((unsigned char)clipped[keep]&0xc0)==0x80) --keep;
        do {
            memcpy(clipped+keep,"...",4);
            C2D_TextParse(&t,text_buf,clipped);
            C2D_TextOptimize(&t);
            C2D_TextGetDimensions(&t,scale,scale,&width,&height);
            if (width<=max_width || !keep) break;
            --keep;
            while (keep && ((unsigned char)clipped[keep]&0xc0)==0x80) --keep;
        } while (keep);
    }
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, scale, scale, color);
}

static void title_ellipsized(char *out, size_t cap, const char *input, size_t chars) {
    size_t n = strlen(input);
    if (n <= chars) { snprintf(out, cap, "%s", input); return; }
    size_t cut = chars > 3 ? chars-3 : 0;
    if (cut >= cap-4) cut = cap-4;
    while (cut && ((unsigned char)input[cut] & 0xc0) == 0x80) --cut;
    memcpy(out, input, cut); out[cut] = 0;
    strcat(out, "...");
}

static void time_text(uint32_t ms, char out[16]) {
    if (!ms) { strcpy(out, "--:--"); return; }
    unsigned secs = ms/1000;
    snprintf(out, 16, "%02u:%02u", secs/60, secs%60);
}

static void reload_results(void) {
    result_count = catalog_filter(matches, OST_MAX_ITEMS, platform, query);
    selected = scroll_at = 0;
}

static const OstEntry *current(void) {
    return selected >= 0 && selected < result_count ? catalog_get(matches[selected]) : NULL;
}

static void select_delta(int delta) {
    if (!result_count) return;
    selected += delta;
    if (selected < 0) selected = 0;
    if (selected >= result_count) selected = result_count-1;
    if (selected < scroll_at) scroll_at = selected;
    if (selected >= scroll_at+LIST_ROWS) scroll_at = selected-LIST_ROWS+1;
}

static void activate(void) {
    const OstEntry *item = current();
    if (!item) return;
    if (!item->available) { say("Exemple uniquement : MP3 absent de la carte SD."); return; }
    if (audio_open(item->path, item->duration_ms)) {
        playing_index = matches[selected];
        say("Lecture lancee.");
    } else {
        playing_index = -1;
        say(audio_error());
    }
}

static void search_keyboard(void) {
    bool resume = audio_playing() && !audio_paused();
    if (resume) audio_toggle_pause();
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, sizeof(query)-1);
    swkbdSetHintText(&keyboard, "Rechercher dans 3DS et Wii U");
    swkbdSetInitialText(&keyboard, query);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Effacer", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Chercher", true);
    char result[sizeof(query)] = {0};
    SwkbdButton button = swkbdInputText(&keyboard, result, sizeof(result));
    if (button == SWKBD_BUTTON_RIGHT) {
        snprintf(query, sizeof(query), "%s", result);
        reload_results();
    } else if (button == SWKBD_BUTTON_LEFT) {
        query[0] = 0; reload_results();
    }
    if (resume && audio_playing()) audio_toggle_pause();
}

static void draw_top(void) {
    C2D_SceneBegin(top);
    C2D_TargetClear(top, navy);
    round_rect(12,12,376,216,14,panel);
    round_rect(20,20,360,36,9,raised);
    label("SHIELD",30,23,.78f,140,white);
    label("OST ARCHIVES",147,29,.56f,145,cyan);
    label("v0.1",341,31,.42f,30,muted);

    /* A compact, code drawn record is a generic music motif, not cover art. */
    C2D_DrawCircleSolid(94,128,0,66,raised);
    C2D_DrawCircleSolid(94,128,0,57,navy);
    C2D_DrawCircleSolid(94,128,0,46,raised);
    C2D_DrawCircleSolid(94,128,0,42,navy);
    C2D_DrawCircleSolid(94,128,0,32,raised);
    C2D_DrawCircleSolid(94,128,0,28,cyan);
    C2D_DrawCircleSolid(94,128,0,7,navy);

    const OstEntry *item = current();
    if (!item) {
        label("Bienvenue dans vos archives",171,76,.64f,198,white);
        label("Profil Internet Archive",171,107,.52f,199,muted);
        label("en attente de configuration",171,128,.48f,199,muted);
        label("Aucune archive chargee",171,173,.49f,200,cyan);
    } else {
        char line[160], tm[16];
        char short_title[96];
        title_ellipsized(short_title,sizeof(short_title),item->title,25);
        label(short_title,171,70,.69f,198,white);
        snprintf(line,sizeof(line),"%s  |  %s",item->platform==OST_3DS ? "3DS" : "Wii U",
                 item->available ? "MP3 local" : "Exemple absent");
        label(line,171,104,.49f,198,item->available ? cyan : amber);
        label(item->collection,171,128,.47f,198,muted);
        if (item->bytes) snprintf(line,sizeof(line),"MP3  |  %lu Ko",(unsigned long)(item->bytes/1024));
        else snprintf(line,sizeof(line),"MP3  |  taille inconnue");
        label(line,171,151,.45f,198,muted);
        bool this_playing = audio_playing() && playing_index == matches[selected];
        label(this_playing ? (audio_paused() ? "PAUSE" : "LECTURE") :
              (item->available ? "PRET A LIRE" : "INDISPONIBLE"),
              171,179,.52f,198,this_playing ? green : muted);
        uint32_t elapsed = this_playing ? audio_position_ms() : 0;
        uint32_t total = this_playing ? audio_duration_ms() : item->duration_ms;
        time_text(elapsed,tm);
        snprintf(line,sizeof(line),"%s / ",tm);
        time_text(total,tm);
        strncat(line,tm,sizeof(line)-strlen(line)-1);
        label(line,171,199,.43f,198,muted);
        round_rect(20,209,360,6,3,raised);
        if (total && elapsed) {
            float fill = 360.0f*elapsed/total;
            if (fill > 360) fill=360;
            if (fill > 6) round_rect(20,209,fill,6,3,cyan);
        }
    }
    if (*message && osGetTime() < message_until) {
        round_rect(26,181,348,30,8,raised);
        label(message,34,186,.49f,331,white);
    }
}

static void draw_bottom(void) {
    C2D_SceneBegin(bottom);
    C2D_TargetClear(bottom,navy);
    if (options_open) {
        round_rect(9,8,302,224,12,panel);
        if (guide_page) {
            label(guide_page==1 ? "GUIDE  1/2" : "CREDITS  2/2",20,16,.72f,280,white);
            if (guide_page==1) {
                label("Deux bibliotheques : 3DS et Wii U",20,53,.49f,280,cyan);
                label("La recherche couvre les deux.",20,79,.46f,280,white);
                label("A : lire   X : pause   B : arreter",20,105,.44f,280,white);
                label("SELECT : options   START : quitter",20,131,.43f,280,white);
                label("Profil Archive encore en attente :",20,166,.43f,280,muted);
                label("aucun telechargement pour l'instant.",20,184,.43f,280,muted);
            } else {
                label("Idee et archives : Neo Foxy76",20,53,.46f,280,cyan);
                label("Conception : Shield-77",20,80,.46f,280,white);
                label("Code : assistance Codex / OpenAI",20,107,.44f,280,white);
                label("Audio : SHIELD DJ Lite + minimp3",20,134,.44f,280,white);
                label("Code GPLv3 ; minimp3 CC0.",20,163,.44f,280,muted);
                label("Aucun audio tiers n'est inclus.",20,183,.44f,280,muted);
            }
            label("B : retour             A : suivant",20,215,.42f,280,white);
            return;
        }
        label("OPTIONS",20,16,.75f,280,white);
        label("Theme visuel",20,52,.57f,260,cyan);
        round_rect(20,80,280,38,9,theme_choice==0 ? cyan : raised);
        label("Bleu cyber",34,88,.59f,250,theme_choice==0 ? navy : white);
        round_rect(20,126,280,38,9,theme_choice==1 ? cyan : raised);
        label("Noir et blanc",34,134,.59f,250,theme_choice==1 ? navy : white);
        label("Idee et archives : Neo Foxy76",20,174,.43f,276,white);
        label("Futur dossier : sdmc:/download audio",20,193,.42f,276,muted);
        label("X/Touch : guide    SELECT/B : retour",20,216,.42f,276,muted);
        return;
    }
    round_rect(9,8,302,37,10,panel);
    label("RECHERCHER",17,17,.53f,105,cyan);
    label(*query ? query : "Toutes les bibliotheques  >",115,18,.45f,185,*query ? white : muted);
    round_rect(9,52,147,29,8,platform == OST_3DS ? cyan : panel);
    round_rect(164,52,147,29,8,platform == OST_WIIU ? cyan : panel);
    label("3DS",67,58,.57f,45,platform == OST_3DS ? navy : white);
    label("Wii U",216,58,.57f,58,platform == OST_WIIU ? navy : white);
    if (*query) label("Recherche globale",12,84,.42f,190,muted);
    else label(platform == OST_3DS ? "Bibliotheque 3DS" : "Bibliotheque Wii U",12,84,.42f,190,muted);
    char count[24]; snprintf(count,sizeof(count),"%d titre(s)",result_count);
    label(count,256,84,.38f,55,muted);
    for (int row=0; row<LIST_ROWS; ++row) {
        int result=scroll_at+row;
        if (result >= result_count) break;
        const OstEntry *item=catalog_get(matches[result]);
        int y=102+row*22;
        if (result == selected) round_rect(9,y,302,21,6,raised);
        char title[96]; title_ellipsized(title,sizeof(title),item->title,31);
        label(title,16,y+2,.45f,229,result == selected ? white : muted);
        label(item->platform==OST_3DS ? "3DS" : "Wii U",246,y+2,.40f,32,cyan);
        C2D_DrawCircleSolid(298,y+10,0,3,item->available ? green : amber);
    }
    if (result_count == 0) {
        label("Aucune archive disponible",25,121,.59f,265,white);
        label("Profil Archive en attente",24,151,.44f,273,muted);
    }
    round_rect(9,215,302,18,6,panel);
    label("A Lire   X Pause   B Stop   SELECT Options",14,218,.40f,292,white);
}

static void render(void) {
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    draw_top(); draw_bottom();
    C3D_FrameEnd(0);
}

int main(void) {
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE) || !C2D_Init(768)) {
        gfxExit(); return 1;
    }
    C2D_Prepare();
    top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    bottom=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    text_buf=C2D_TextBufNew(4096);
    if (!top || !bottom || !text_buf) {
        if (text_buf) C2D_TextBufDelete(text_buf);
        C2D_Fini(); C3D_Fini(); gfxExit(); return 1;
    }
    load_theme();
    bool sound_ready=audio_init();
    catalog_reload(); reload_results();
    if (!sound_ready) say(audio_error());
    int touch_origin=-1;
    while (aptMainLoop()) {
        hidScanInput();
        uint32_t down=hidKeysDown(), up=hidKeysUp();
        if (down & KEY_START) break;
        bool was_playing=audio_playing();
        audio_update();
        if (was_playing && !audio_playing()) {
            playing_index=-1;
            say(*audio_error() ? audio_error() : "Lecture terminee.");
        }
        if (down & KEY_SELECT) { options_open=!options_open; guide_page=0; }
        if (options_open) {
            if (guide_page) {
                if ((down & (KEY_B|KEY_DLEFT)) && guide_page>1) --guide_page;
                else if (down & (KEY_B|KEY_DLEFT)) guide_page=0;
                if (down & (KEY_A|KEY_DRIGHT)) guide_page=guide_page<2 ? guide_page+1 : 0;
                if (down & KEY_TOUCH) {
                    touchPosition touch; hidTouchRead(&touch);
                    if (touch.py>=207 && touch.px>=160)
                        guide_page=guide_page<2 ? guide_page+1 : 0;
                    else if (touch.py>=207)
                        guide_page=guide_page>1 ? guide_page-1 : 0;
                }
                render();
                continue;
            }
            if (down & KEY_B) options_open=false;
            if (down & KEY_X) guide_page=1;
            if (down & KEY_DUP) { set_theme(0); save_theme(); }
            if (down & KEY_DDOWN) { set_theme(1); save_theme(); }
            if (down & KEY_A) { save_theme(); say("Theme enregistre."); }
            if (down & KEY_TOUCH) {
                touchPosition touch; hidTouchRead(&touch);
                if (touch.py>=80 && touch.py<118) { set_theme(0); save_theme(); }
                if (touch.py>=126 && touch.py<164) { set_theme(1); save_theme(); }
                if (touch.py>=207 && touch.px<160) guide_page=1;
            }
            render();
            continue;
        }
        if (down & KEY_DUP) select_delta(-1);
        if (down & KEY_DDOWN) select_delta(1);
        if (down & KEY_L) select_delta(-LIST_ROWS);
        if (down & KEY_R) select_delta(LIST_ROWS);
        if (down & KEY_A) activate();
        if (down & KEY_X) audio_toggle_pause();
        if (down & KEY_B) {
            if (audio_playing()) { audio_stop(); playing_index=-1; say("Lecture arretee."); }
            else if (*query) { query[0]=0; reload_results(); }
        }
        if (down & KEY_Y) {
            audio_stop(); playing_index=-1;
            catalog_reload(); reload_results(); say("Profil non configure pour le moment.");
        }
        if (down & KEY_TOUCH) {
            touchPosition touch; hidTouchRead(&touch);
            if (touch.py < 45) { search_keyboard(); touch_origin=-1; }
            else if (touch.py >= 52 && touch.py < 82) {
                platform = touch.px >= 160 ? OST_WIIU : OST_3DS;
                if (!*query) reload_results();
                touch_origin=-1;
            } else if (touch.py >= 102 && touch.py < 212) {
                int row=(touch.py-102)/22;
                touch_origin=scroll_at+row;
            }
        }
        if (up & KEY_TOUCH && touch_origin >= 0) {
            int row=touch_origin; touch_origin=-1;
            if (row < result_count) {
                if (selected == row) activate();
                else { selected=row; }
            }
        }
        render();
    }
    audio_exit();
    C2D_TextBufDelete(text_buf);
    C2D_Fini(); C3D_Fini(); gfxExit();
    return 0;
}
