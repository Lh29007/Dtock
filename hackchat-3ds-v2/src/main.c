#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "hc.h"
#include "ui.h"
#include "ws.h"

#define SOC_BUF_SIZE 0x100000
#define CA_FILE      "romfs:/cacert.pem"
#define HC_HOST      "hack.chat"
#define HC_PORT      "443"
#define HC_PATH      "/chat-ws"
#define LOG_STEP     5

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
    default:
        break;
    }
}

static void poll_server(void)
{
    char in[8192];
    if (!g_ws)
        return;
    for (int k = 0; k < 32; k++) {
        int n = ws_poll_text(g_ws, in, sizeof in);
        if (n < 0) {
            ws_close(g_ws);
            g_ws = NULL;
            g_st.connected = 0;
            hc_log_info(&g_st, "Connexion perdue. Appuie sur Reconnecter.");
            return;
        }
        if (n == 0)
            break;
        hc_handle(&g_st, in);
    }
}

int main(int argc, char **argv)
{
    void *soc_buf;
    char nick[32];
    char channel[64];

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

    /* Pseudo et salon de départ (clavier natif de la console) */
    ask_text("Pseudo (lettres, chiffres, _)", NULL, nick, sizeof nick, 24);
    if (!nick[0])
        snprintf(nick, sizeof nick, "anon3ds");
    ask_text("Salon de départ", "programming", channel, sizeof channel, 32);
    if (!channel[0])
        snprintf(channel, sizeof channel, "programming");

    hc_init(&g_st, nick);
    memset(&g_view, 0, sizeof g_view);

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
