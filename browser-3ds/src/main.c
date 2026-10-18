#include <3ds.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"
#include "http.h"

#define SOC_BUF_SIZE 0x100000
#define CA_FILE      "romfs:/cacert.pem"
#define ROWS         28            /* lignes de texte sur l'écran du haut */
#define MAX_HIST     32

static PrintConsole g_top, g_bottom;

static char g_url[HTTP_MAX_URL];   /* page affichée */
static int  g_status;              /* dernier code HTTP (0 = aucun) */
static html_page g_page;
static int  g_scroll;
static char g_hist[MAX_HIST][HTTP_MAX_URL];
static int  g_nhist;
static int  g_dirty = 1;

static void frame(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void ask_text(const char *hint, const char *initial, char *out, size_t cap)
{
    SwkbdState st;
    out[0] = '\0';
    swkbdInit(&st, SWKBD_TYPE_NORMAL, 2, (int)cap - 1);
    swkbdSetHintText(&st, hint);
    if (initial && *initial)
        swkbdSetInitialText(&st, initial);
    swkbdInputText(&st, out, cap);
}

static void ask_number(const char *hint, char *out, size_t cap)
{
    SwkbdState st;
    out[0] = '\0';
    swkbdInit(&st, SWKBD_TYPE_NUMPAD, 2, 4);
    swkbdSetHintText(&st, hint);
    swkbdInputText(&st, out, cap);
}

static void push_hist(const char *url)
{
    if (g_nhist == MAX_HIST) {
        memmove(g_hist[0], g_hist[1], sizeof g_hist[0] * (MAX_HIST - 1));
        g_nhist--;
    }
    snprintf(g_hist[g_nhist++], HTTP_MAX_URL, "%s", url);
}

/* Vrai si le contenu n'a pas l'air d'être du texte (image, binaire, etc.). */
static int looks_binary(const unsigned char *b, size_t len)
{
    size_t n = len < 512 ? len : 512;
    for (size_t i = 0; i < n; i++)
        if (b[i] == 0)
            return 1;
    return 0;
}

static void navigate(const char *url, int push)
{
    http_result res;
    char msg[HTTP_MAX_URL + 256];

    if (push && g_url[0])
        push_hist(g_url);

    consoleSelect(&g_bottom);
    printf("\x1b[2J\x1b[H");
    printf("Chargement...\n%.38s\n", url);
    consoleSelect(&g_top);
    frame();

    html_page_free(&g_page);
    g_scroll = 0;
    g_status = 0;

    if (http_get(url, CA_FILE, &res) != 0) {
        snprintf(msg, sizeof msg, "Impossible d'ouvrir la page.\n\n%s\n\nURL : %s",
                 http_error(), url);
        html_plain(msg, &g_page);
        snprintf(g_url, sizeof g_url, "%s", url);
    } else {
        g_status = res.status;
        snprintf(g_url, sizeof g_url, "%s", res.final_url);

        if (looks_binary(res.body, res.len)) {
            snprintf(msg, sizeof msg,
                     "Contenu non texte (image, fichier...) : non affiché.\n\nURL : %s",
                     g_url);
            html_plain(msg, &g_page);
        } else {
            html_render((const char *)res.body, res.len, g_url, &g_page);
        }
    }

    http_result_free(&res);
    g_dirty = 1;
}

static void go_back(void)
{
    if (g_nhist == 0)
        return;
    {
        char prev[HTTP_MAX_URL];
        snprintf(prev, sizeof prev, "%s", g_hist[--g_nhist]);
        navigate(prev, 0);
    }
}

static void draw_top(void)
{
    consoleSelect(&g_top);
    printf("\x1b[2J\x1b[H");
    for (int i = 0; i < ROWS && g_scroll + i < g_page.nlines; i++)
        printf("%s\n", g_page.lines[g_scroll + i]);
}

static void draw_bottom(void)
{
    consoleSelect(&g_bottom);
    printf("\x1b[2J\x1b[H");
    printf("%.38s\n", g_url[0] ? g_url : "(aucune page)");
    if (g_status)
        printf("HTTP %d\n", g_status);
    printf("Liens : %d   Ligne %d/%d\n\n", g_page.nlinks,
           g_page.nlines ? g_scroll + 1 : 0, g_page.nlines);
    printf("HAUT/BAS : defiler\nL/R : page\n");
    printf("A : ouvrir un lien\nX : adresse\n");
    printf("B : retour   Y : recharger\n");
    printf("START : quitter\n");
    consoleSelect(&g_top);
}

static void redraw(void)
{
    draw_top();
    draw_bottom();
    g_dirty = 0;
}

static int max_scroll(void)
{
    int m = g_page.nlines - ROWS;
    return m > 0 ? m : 0;
}

int main(int argc, char **argv)
{
    void *soc_buf;
    int running = 1;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, &g_top);
    consoleInit(GFX_BOTTOM, &g_bottom);
    consoleSelect(&g_top);

    if (R_FAILED(psInit())) {
        printf("psInit a échoué.\n");
        goto quit_gfx;
    }
    if (R_FAILED(romfsInit())) {
        printf("romfsInit a échoué.\n");
        goto quit_ps;
    }

    soc_buf = memalign(0x1000, SOC_BUF_SIZE);
    if (!soc_buf || R_FAILED(socInit(soc_buf, SOC_BUF_SIZE))) {
        printf("Réseau (socInit) indisponible.\n");
        goto quit_romfs;
    }

    html_plain("Navigateur 3DS\n\n"
               "Appuie sur X pour saisir une adresse https://...\n\n"
               "Le texte des pages s'affiche ici, avec les liens numerotes [1], [2]...\n"
               "Pas de JavaScript ni de CSS : seul le texte est rendu.",
               &g_page);
    g_url[0] = '\0';

    while (aptMainLoop() && running) {
        hidScanInput();
        u32 down = hidKeysDownRepeat();

        if (down & KEY_START) {
            running = 0;
            break;
        }

        if (down & KEY_DOWN) {
            if (g_scroll < max_scroll()) g_scroll++;
            g_dirty = 1;
        }
        if (down & KEY_UP) {
            if (g_scroll > 0) g_scroll--;
            g_dirty = 1;
        }
        if (down & KEY_R) {
            g_scroll += ROWS - 2;
            if (g_scroll > max_scroll()) g_scroll = max_scroll();
            g_dirty = 1;
        }
        if (down & KEY_L) {
            g_scroll -= ROWS - 2;
            if (g_scroll < 0) g_scroll = 0;
            g_dirty = 1;
        }

        if (down & KEY_X) {
            char addr[HTTP_MAX_URL];
            ask_text("Adresse (https://...)", "https://", addr, sizeof addr);
            if (addr[0])
                navigate(addr, 1);
        }

        if ((down & KEY_A) && g_page.nlinks > 0) {
            char num[8];
            int n;
            ask_number("Numero du lien", num, sizeof num);
            n = atoi(num);
            if (n >= 1 && n <= g_page.nlinks) {
                char target[HTTP_MAX_URL];
                snprintf(target, sizeof target, "%s", g_page.links[n - 1]);
                navigate(target, 1);
            }
        }

        if (down & KEY_B)
            go_back();

        if (down & KEY_Y && g_url[0])
            navigate(g_url, 0);

        if (g_dirty)
            redraw();
        frame();
    }

    html_page_free(&g_page);
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
