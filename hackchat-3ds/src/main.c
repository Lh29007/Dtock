#include <3ds.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "hc.h"
#include "ws.h"

#define SOC_BUF_SIZE  0x100000
#define CA_FILE       "romfs:/cacert.pem"
#define HC_HOST       "hack.chat"
#define HC_PORT       "443"
#define HC_PATH       "/chat-ws"

static PrintConsole g_top, g_bottom;

static void frame(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void ask_text(const char *hint, const char *initial, char *out, size_t cap, int maxlen)
{
    SwkbdState st;
    out[0] = '\0';
    swkbdInit(&st, SWKBD_TYPE_NORMAL, 2, maxlen);
    swkbdSetHintText(&st, hint);
    if (initial && *initial)
        swkbdSetInitialText(&st, initial);
    swkbdInputText(&st, out, cap);
}

/* Affiche une erreur fatale et attend START pour quitter. */
static void fatal(const char *msg)
{
    consoleSelect(&g_top);
    printf("\x1b[31m%s\x1b[0m\n", msg);
    printf("Appuie sur START pour quitter.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        frame();
    }
}

int main(int argc, char **argv)
{
    static char nick[32];
    static char channel[64];
    static char buf[1024];
    static char in[8192];
    hc_state st;
    ws_conn *ws = NULL;
    void *soc_buf;
    int rc;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, &g_top);
    consoleInit(GFX_BOTTOM, &g_bottom);
    consoleSelect(&g_top);

    if (R_FAILED(psInit())) { fatal("psInit a échoué (service PS)."); goto quit_gfx; }
    if (R_FAILED(romfsInit())) { fatal("romfsInit a échoué."); goto quit_ps; }

    soc_buf = memalign(0x1000, SOC_BUF_SIZE);
    if (!soc_buf || R_FAILED(socInit(soc_buf, SOC_BUF_SIZE))) {
        fatal("Réseau (socInit) indisponible.");
        goto quit_romfs;
    }

    memset(&st, 0, sizeof st);

    consoleSelect(&g_bottom);
    printf("\x1b[2J");
    printf("A : envoyer un message\nSTART : quitter\n");
    consoleSelect(&g_top);

    ask_text("Pseudo (lettres, chiffres, _)", NULL, nick, sizeof nick, 24);
    if (!nick[0]) snprintf(nick, sizeof nick, "anon3ds");
    ask_text("Salon", "programming", channel, sizeof channel, 32);
    if (!channel[0]) snprintf(channel, sizeof channel, "programming");

    printf("Connexion à %s:%s ...\n", HC_HOST, HC_PORT);
    frame();

    rc = ws_connect(&ws, HC_HOST, HC_PORT, HC_PATH, CA_FILE);
    if (rc != 0) {
        char err[128];
        snprintf(err, sizeof err, "Échec de connexion (code %d). Vérifie le Wi-Fi.", rc);
        fatal(err);
        goto quit_soc;
    }

    hc_build_join(buf, sizeof buf, nick, channel);
    if (ws_send_text(ws, buf) < 0) {
        fatal("Impossible d'envoyer la commande join.");
        goto quit_ws;
    }
    printf("Rejoint ?%s en tant que %s\n", channel, nick);

    while (aptMainLoop()) {
        int n;

        hidScanInput();
        u32 down = hidKeysDown();

        if (down & KEY_START)
            break;

        if (down & KEY_A) {
            char text[256];
            ask_text("Message", NULL, text, sizeof text, 200);
            if (text[0]) {
                hc_build_chat(buf, sizeof buf, st.channel[0] ? st.channel : channel, text);
                if (ws_send_text(ws, buf) < 0)
                    printf("[!] Envoi impossible\n");
            }
        }

        n = ws_poll_text(ws, in, sizeof in);
        if (n < 0) {
            printf("[!] Connexion perdue.\n");
            break;
        }
        if (n > 0)
            hc_handle(&st, in);

        frame();
    }

quit_ws:
    ws_close(ws);
quit_soc:
    socExit();
    free(soc_buf);
quit_romfs:
    romfsExit();
quit_ps:
    psExit();
quit_gfx:
    gfxExit();
    return 0;
}
