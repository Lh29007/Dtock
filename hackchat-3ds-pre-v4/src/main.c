#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "update.h"

#define SOC_BUF_SIZE 0x100000
#define MAX_LINES    12
#define LINE_LEN     96

#define RGBA8(r, g, b) ((uint32_t)(0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))
#define C_TEXT   RGBA8(0x1A, 0x1A, 0x1A)
#define C_GREY   RGBA8(0x80, 0x80, 0x80)
#define C_HEADER RGBA8(0xDA, 0xDA, 0xDA)
#define C_BG_BOT RGBA8(0xF2, 0xF2, 0xF2)
#define C_WHITE  RGBA8(0xFF, 0xFF, 0xFF)

static char g_lines[MAX_LINES][LINE_LEN];
static int  g_nlines;
static int  g_busy;
static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_tb;

static void push_line(const char *s)
{
    if (g_nlines == MAX_LINES) {
        memmove(g_lines[0], g_lines[1], sizeof g_lines[0] * (MAX_LINES - 1));
        g_nlines--;
    }
    snprintf(g_lines[g_nlines++], LINE_LEN, "%s", s);
}

static void text(float x, float y, float scale, uint32_t color, const char *s)
{
    C2D_Text t;
    C2D_TextParse(&t, g_tb, s);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static void rect(float x, float y, float w, float h, uint32_t color)
{
    C2D_DrawRectSolid(x, y, 0.0f, w, h, color);
}

static void draw(void)
{
    int i;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_tb);

    C2D_TargetClear(g_top, C_WHITE);
    C2D_SceneBegin(g_top);
    rect(0, 0, 400, 28, C_HEADER);
    text(8.0f, 8.0f, 0.6f, C_TEXT, "hack.chat : mise à jour V5");
    for (i = 0; i < g_nlines; i++)
        text(8.0f, 36.0f + i * 16.0f, 0.5f, C_TEXT, g_lines[i]);

    C2D_TargetClear(g_bot, C_BG_BOT);
    C2D_SceneBegin(g_bot);
    rect(0, 0, 320, 28, C_HEADER);
    text(8.0f, 8.0f, 0.6f, C_TEXT, "Pre-V4");
    text(8.0f, 50.0f, 0.5f, C_TEXT,
         g_busy ? "Téléchargement en cours..." : "Terminé.");
    text(8.0f, 70.0f, 0.5f, C_GREY,
         "Le fichier est dans sdmc:/3ds/hackchat/");
    text(8.0f, 220.0f, 0.5f, C_GREY, "A : relancer la mise à jour    START : quitter");

    C3D_FrameEnd(0);
}

static void log_cb(const char *line, void *ctx)
{
    (void)ctx;
    push_line(line);
    draw();
}

static void run_update_now(void)
{
    g_busy = 1;
    g_nlines = 0;
    draw();
    update_run(log_cb, NULL);
    g_busy = 0;
    draw();
}

int main(int argc, char **argv)
{
    void *soc_buf = NULL;
    int ps_ok = 0;
    int net_ok = 0;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_tb = C2D_TextBufNew(4096);

    if (R_SUCCEEDED(psInit()) && R_SUCCEEDED(romfsInit())) {
        ps_ok = 1;
        soc_buf = memalign(0x1000, SOC_BUF_SIZE);
        if (soc_buf && R_SUCCEEDED(socInit(soc_buf, SOC_BUF_SIZE)))
            net_ok = 1;
    }

    if (net_ok) {
        /* mise à jour automatique au lancement */
        run_update_now();
    } else {
        push_line("Erreur : réseau ou romfs indisponible.");
    }
    draw();

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();

        if (down & KEY_START)
            break;
        if ((down & KEY_A) && net_ok)
            run_update_now();

        draw();
    }

    if (net_ok)
        socExit();
    free(soc_buf);
    if (ps_ok) {
        romfsExit();
        psExit();
    }

    C2D_TextBufDelete(g_tb);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
