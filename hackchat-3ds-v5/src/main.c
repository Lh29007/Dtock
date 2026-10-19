#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "hc.h"
#include "ui.h"
#include "ws.h"

#define SOC_BUF_SIZE 0x100000
#define CA_FILE      "romfs:/cacert.pem"
#define HC_HOST      "hack.chat"
#define HC_PORT      "443"
#define HC_PATH      "/chat-ws"
#define LOG_STEP     5
#define CFG_DIR      "sdmc:/3ds/hackchat"
#define CFG_FILE     "sdmc:/3ds/hackchat/config.txt"

static hc_state g_st;
static ui_view  g_view;
static ws_conn *g_ws;
static char     g_last_channel[64];

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

/* ---------- Configuration (pseudo, thème) sur la carte SD ---------- */

static void cfg_load(char *nick, size_t ncap, int *dark)
{
    FILE *f = fopen(CFG_FILE, "r");
    char line[96];

    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        if (!strncmp(line, "nick=", 5))
            snprintf(nick, ncap, "%s", line + 5);
        else if (!strncmp(line, "dark=", 5))
            *dark = (line[5] == '1');
    }
    fclose(f);
}

static void cfg_save(const char *nick, int dark)
{
    FILE *f;

    mkdir("sdmc:/3ds", 0777);
    mkdir(CFG_DIR, 0777);
    f = fopen(CFG_FILE, "w");
    if (!f)
        return;
    fprintf(f, "nick=%s\ndark=%d\n", nick, dark ? 1 : 0);
    fclose(f);
}

/* ---------- Connexion et salons ---------- */

static int connect_server(void)
{
    int rc;
    if (g_ws) {
        ws_close(g_ws);
        g_ws = NULL;
    }
    rc = ws_connect(&g_ws, HC_HOST, HC_PORT, HC_PATH, CA_FILE);
    if (rc != 0) {
        g_ws = NULL;
        g_st.connected = 0;
        hc_log_info(&g_st, "Connexion impossible. Vérifie le Wi-Fi.");
        return -1;
    }
    g_st.connected = 1;
    hc_log_info(&g_st, "Connecté à hack.chat.");
    return 0;
}

static void send_json(const char *json)
{
    if (!g_ws) {
        hc_log_info(&g_st, "Pas de connexion.");
        return;
    }
    if (ws_send_text(g_ws, json) < 0)
        hc_log_info(&g_st, "Envoi impossible.");
}

static void join_channel(const char *chan)
{
    char json[512];
    if (!chan || !*chan)
        return;
    hc_clear_users(&g_st);
    snprintf(g_last_channel, sizeof g_last_channel, "%s", chan);
    {
        char info[128];
        snprintf(info, sizeof info, "Connexion au salon ?%s...", chan);
        hc_log_info(&g_st, info);
    }
    hc_build_join(json, sizeof json, g_st.nick, chan);
    send_json(json);
    /* demande la liste des salons publics */
    hc_build_getchannels(json, sizeof json);
    send_json(json);
}

static void leave_channel(void)
{
    char json[512];
    char info[128];
    if (!g_st.channel[0])
        return;
    hc_build_leave(json, sizeof json, g_st.channel);
    send_json(json);
    snprintf(info, sizeof info, "Tu as quitté ?%s", g_st.channel);
    hc_log_info(&g_st, info);
    g_st.channel[0] = '\0';
    hc_clear_users(&g_st);
}

static void refresh_channels(void)
{
    char json[64];
    hc_build_getchannels(json, sizeof json);
    send_json(json);
}

static void compose(void)
{
    char text[256];
    char json[1200];

    ask_text("Message", NULL, text, sizeof text, 200);
    if (!text[0])
        return;
    if (!g_st.channel[0]) {
        hc_log_info(&g_st, "Rejoins un salon d'abord (liste en bas).");
        return;
    }
    hc_build_chat(json, sizeof json, g_st.channel, text);
    send_json(json);
}

/* Changement de pseudo : marche hors salon ; dans un salon, on le quitte puis on le rejoint. */
static void change_nick(void)
{
    char nick[32];
    char chan[64];
    int was_in = g_st.channel[0] != '\0';

    ask_text("Nouveau pseudo", g_st.nick, nick, sizeof nick, 24);
    if (!nick[0] || !strcmp(nick, g_st.nick))
        return;

    snprintf(g_st.nick, sizeof g_st.nick, "%s", nick);
    cfg_save(g_st.nick, g_view.dark);
    {
        char info[96];
        snprintf(info, sizeof info, "Pseudo changé : %s", g_st.nick);
        hc_log_info(&g_st, info);
    }

    if (g_ws && was_in) {
        char json[512];
        snprintf(chan, sizeof chan, "%s", g_st.channel);
        hc_build_leave(json, sizeof json, chan);
        send_json(json);
        g_st.channel[0] = '\0';
        hc_clear_users(&g_st);
        join_channel(chan);
    }
}

static void select_channel(int idx)
{
    if (idx < 0 || idx >= g_st.nchannels)
        return;
    g_st.selected_channel = idx;
    if (strcmp(g_st.channels[idx], g_st.channel) != 0)
        join_channel(g_st.channels[idx]);
}

static void keep_selection_visible(void)
{
    if (g_st.selected_channel < g_view.list_offset)
        g_view.list_offset = g_st.selected_channel;
    if (g_st.selected_channel >= g_view.list_offset + UI_LIST_ROWS)
        g_view.list_offset = g_st.selected_channel - UI_LIST_ROWS + 1;
    if (g_view.list_offset < 0)
        g_view.list_offset = 0;
}

static void handle_touch(ui_hit h)
{
    switch (h.kind) {
    case UI_HIT_CHANNEL:
        select_channel(h.index);
        break;
    case UI_HIT_REFRESH:
        refresh_channels();
        break;
    case UI_HIT_LEAVE:
        leave_channel();
        break;
    case UI_HIT_RECONNECT:
        if (!g_st.connected) {
            if (connect_server() == 0 && g_last_channel[0])
                join_channel(g_last_channel);
        }
        break;
    case UI_HIT_INPUT:
        compose();
        break;
    case UI_HIT_SETTINGS:
        g_view.screen = UI_SCREEN_SETTINGS;
        break;
    case UI_HIT_BACK:
        g_view.screen = UI_SCREEN_CHAT;
        break;
    case UI_HIT_NICK:
        change_nick();
        break;
    case UI_HIT_THEME:
        g_view.dark = !g_view.dark;
        cfg_save(g_st.nick, g_view.dark);
        break;
    default:
        break;
    }
}

/* Réception : statique (pas sur la pile) et assez grande pour un onlineSet de salon chargé. */
static char g_in[32768];
static u64  g_retry_at = 0;     /* heure (ms) de la prochaine tentative, 0 = aucune */
static u32  g_retry_s = 2;      /* délai courant, double à chaque échec (plafonné à 60 s) */

static void schedule_retry(void)
{
    char info[96];
    g_retry_at = osGetTime() + (u64)g_retry_s * 1000ULL;
    snprintf(info, sizeof info, "Reconnexion dans %lu s...", (unsigned long)g_retry_s);
    hc_log_info(&g_st, info);
    if (g_retry_s < 60)
        g_retry_s *= 2;
}

static void poll_server(void)
{
    if (!g_ws)
        return;
    for (int k = 0; k < 32; k++) {
        int n = ws_poll_text(g_ws, g_in, sizeof g_in);
        if (n < 0) {
            ws_close(g_ws);
            g_ws = NULL;
            g_st.connected = 0;
            schedule_retry();
            return;
        }
        if (n == 0)
            break;
        hc_handle(&g_st, g_in);
    }
}

int main(int argc, char **argv)
{
    void *soc_buf;
    char nick[32] = "";
    char channel[64];
    int dark = 0;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    ui_init();

    if (R_FAILED(psInit()) || R_FAILED(romfsInit())) {
        goto fail_gfx;
    }
    soc_buf = memalign(0x1000, SOC_BUF_SIZE);
    if (!soc_buf || R_FAILED(socInit(soc_buf, SOC_BUF_SIZE)))
        goto fail_gfx;

    /* Pseudo mémorisé ; sinon demandé. Salon de départ demandé à chaque lancement. */
    cfg_load(nick, sizeof nick, &dark);
    if (!nick[0])
        ask_text("Pseudo (lettres, chiffres, _)", NULL, nick, sizeof nick, 24);
    if (!nick[0])
        snprintf(nick, sizeof nick, "anon3ds");
    ask_text("Salon de départ", "programming", channel, sizeof channel, 32);
    if (!channel[0])
        snprintf(channel, sizeof channel, "programming");

    hc_init(&g_st, nick);
    memset(&g_view, 0, sizeof g_view);
    g_view.dark = dark;
    cfg_save(g_st.nick, g_view.dark);

    if (connect_server() == 0)
        join_channel(channel);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();

        if (down & KEY_START)
            break;

        if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            handle_touch(ui_hit_test(&g_st, &g_view, tp.px, tp.py));
        }

        if (down & KEY_SELECT)
            g_view.screen = (g_view.screen == UI_SCREEN_SETTINGS) ? UI_SCREEN_CHAT
                                                                  : UI_SCREEN_SETTINGS;
        if (down & KEY_A)
            compose();
        if (down & KEY_X)
            g_view.panel_open = !g_view.panel_open;
        if (down & KEY_B)
            leave_channel();
        if (down & KEY_Y)
            select_channel(g_st.selected_channel);
        if (down & KEY_UP) {
            if (g_st.selected_channel > 0)
                g_st.selected_channel--;
            keep_selection_visible();
        }
        if (down & KEY_DOWN) {
            if (g_st.selected_channel + 1 < g_st.nchannels)
                g_st.selected_channel++;
            keep_selection_visible();
        }
        if (down & KEY_L) {
            g_view.log_scroll += LOG_STEP;
            if (g_view.log_scroll > HC_MAX_LOG * 4)
                g_view.log_scroll = HC_MAX_LOG * 4;
        }
        if (down & KEY_R) {
            g_view.log_scroll -= LOG_STEP;
            if (g_view.log_scroll < 0)
                g_view.log_scroll = 0;
        }

        /* reconnexion automatique quand la connexion est tombée */
        if (!g_ws && g_retry_at && osGetTime() >= g_retry_at) {
            g_retry_at = 0;
            if (connect_server() == 0) {
                g_retry_s = 2;
                if (g_last_channel[0])
                    join_channel(g_last_channel);
            } else {
                schedule_retry();
            }
        }

        poll_server();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_draw(&g_st, &g_view);
        C3D_FrameEnd(0);
    }

    if (g_ws)
        ws_close(g_ws);
    socExit();
    free(soc_buf);
    romfsExit();
    psExit();

fail_gfx:
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
