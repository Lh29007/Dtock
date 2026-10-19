#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <stdio.h>
#include <string.h>

#include "ui.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define LOG_LINE_H    14
#define LOG_NICK_X_R  96           /* bord droit de la colonne des pseudos (panneau fermé) */
#define LOG_BODY_X    104
#define LOG_BODY_W    (TOP_W - LOG_BODY_X - 6)
#define LOG_NICK_X_R_P 60          /* idem, panneau ouvert : journal réduit à gauche */
#define LOG_BODY_X_P  66
#define LOG_BODY_W_P  (PANEL_X - LOG_BODY_X_P - 6)
#define TEXT_SCALE    0.5f
#define MAX_VLINES    2000
#define PANEL_X       224
#define PANEL_W       (TOP_W - PANEL_X)
#define LIST_ROWS     UI_LIST_ROWS
#define LIST_ROW_H    18
#define LIST_X        8
#define LIST_Y        36
#define LIST_W        304
#define LIST_H        (LIST_ROWS * LIST_ROW_H + 18)
#define BTN_Y         (LIST_Y + LIST_H + 6)
#define BTN_H         26
#define INPUT_Y       (BTN_Y + BTN_H + 8)
#define INPUT_H       38

/* Profondeur citro2d : plus la valeur est grande, plus l'élément est au premier plan. */
#define Z_LOG        0.5f
#define Z_PANEL_BG   0.8f
#define Z_PANEL_TXT  0.9f

/* Couleurs (format citro2d 0xAABBGGRR) */
#define C_WHITE      C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define C_TEXT       C2D_Color32(0x1A, 0x1A, 0x1A, 0xFF)
#define C_GREY       C2D_Color32(0x80, 0x80, 0x80, 0xFF)
#define C_BG_BOT     C2D_Color32(0xF2, 0xF2, 0xF2, 0xFF)
#define C_HEADER     C2D_Color32(0xDA, 0xDA, 0xDA, 0xFF)
#define C_BORDER     C2D_Color32(0xB8, 0xB8, 0xB8, 0xFF)
#define C_SELECT     C2D_Color32(0xCC, 0xE0, 0xFF, 0xFF)
#define C_BTN        C2D_Color32(0x3C, 0x7D, 0xD8, 0xFF)
#define C_BTN_OFF    C2D_Color32(0x9A, 0x9A, 0x9A, 0xFF)
#define C_PANEL      C2D_Color32(0x2B, 0x2B, 0x2B, 0xFF)
#define C_PANEL_TXT  C2D_Color32(0xE6, 0xE6, 0xE6, 0xFF)
#define C_OK         C2D_Color32(0x2E, 0xB8, 0x4B, 0xFF)
#define C_KO         C2D_Color32(0xD6, 0x3B, 0x3B, 0xFF)

typedef struct {
    int first;                /* première ligne d'un message (affiche le pseudo) */
    int kind;                 /* hc_msg_kind */
    char nick[32];
    uint32_t nick_color;
    char body[64];
    uint32_t body_color;
} vline;

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_tb;
static vline g_vlines[MAX_VLINES];
static int g_nvlines;
static float g_z = Z_LOG;        /* profondeur courante pour texte et rectangles */
static float g_char_w = 6.0f;    /* largeur moyenne d'un caractère à TEXT_SCALE */

/* ---------- Texte ---------- */

static void text_draw(float x, float y, float scale, uint32_t color, const char *s)
{
    C2D_Text t;
    C2D_TextParse(&t, g_tb, s);
    C2D_DrawText(&t, C2D_WithColor, x, y, g_z, scale, scale, color);
}

static float text_width(float scale, const char *s)
{
    C2D_Text t;
    float w = 0, h = 0;
    C2D_TextParse(&t, g_tb, s);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static void rect(float x, float y, float w, float h, uint32_t color)
{
    C2D_DrawRectSolid(x, y, g_z, w, h, color);
}

/* ---------- Mise en lignes du journal ---------- */

static void vline_push(const vline *v)
{
    if (g_nvlines < MAX_VLINES)
        g_vlines[g_nvlines++] = *v;
}

static int wrap_chars_for(int body_w)
{
    int n = (int)((float)body_w / g_char_w);
    if (n < 8) n = 8;
    if (n > 60) n = 60;      /* taille du buffer body[] */
    return n;
}

static void wrap_body(const hc_msg *m, const char *prefix, int wrap)
{
    const char *p = m->text;
    int first = 1;
    char body[64];

    if (!*p) {
        vline v;
        memset(&v, 0, sizeof v);
        v.first = 1;
        v.kind = m->kind;
        snprintf(v.nick, sizeof v.nick, "%s", m->nick);
        v.nick_color = m->color;
        v.body_color = C_TEXT;
        snprintf(v.body, sizeof v.body, "%s", prefix ? prefix : "");
        vline_push(&v);
        return;
    }

    while (*p) {
        size_t n = 0;
        size_t take;
        vline v;

        memset(&v, 0, sizeof v);
        v.first = first;
        v.kind = m->kind;
        if (first) {
            snprintf(v.nick, sizeof v.nick, "%s", m->nick);
            v.nick_color = m->color;
        }
        v.body_color = (m->kind == HC_MSG_INFO || m->kind == HC_MSG_JOIN ||
                        m->kind == HC_MSG_LEAVE) ? C_GREY : C_TEXT;

        /* coupe au dernier espace avant la largeur max si possible */
        while (p[n] && n < (size_t)wrap)
            n++;
        if (p[n]) {
            take = n;
            while (take > 0 && p[take] != ' ')
                take--;
            if (take == 0)
                take = n;
        } else {
            take = n;
        }
        if (take >= sizeof body)
            take = sizeof body - 1;
        memcpy(body, p, take);
        body[take] = '\0';
        snprintf(v.body, sizeof v.body, "%s%s", (first && prefix) ? prefix : "", body);
        vline_push(&v);

        p += take;
        while (*p == ' ')
            p++;
        first = 0;
    }
}

static void build_vlines(const hc_state *st, int wrap)
{
    g_nvlines = 0;
    for (int i = 0; i < st->nlog; i++) {
        const hc_msg *m = &st->log[i];
        switch (m->kind) {
        case HC_MSG_CHAT:
            wrap_body(m, NULL, wrap);
            break;
        case HC_MSG_EMOTE: {
            char pre[64];
            snprintf(pre, sizeof pre, "* %s ", m->nick);
            hc_msg tmp = *m;
            tmp.nick[0] = '\0';
            wrap_body(&tmp, pre, wrap);
            break;
        }
        case HC_MSG_JOIN:
        case HC_MSG_LEAVE: {
            char pre[64];
            snprintf(pre, sizeof pre, "%s ", m->nick);
            hc_msg tmp = *m;
            tmp.nick[0] = '\0';
            wrap_body(&tmp, pre, wrap);
            break;
        }
        case HC_MSG_INFO:
        default: {
            hc_msg tmp = *m;
            tmp.nick[0] = '\0';
            wrap_body(&tmp, NULL, wrap);
            break;
        }
        }
    }
}

/* ---------- Écran du haut ---------- */

static void draw_top(const hc_state *st, const ui_view *v)
{
    int panel = v->panel_open;
    int nick_r = panel ? LOG_NICK_X_R_P : LOG_NICK_X_R;
    int body_x = panel ? LOG_BODY_X_P : LOG_BODY_X;
    int body_w = panel ? LOG_BODY_W_P : LOG_BODY_W;
    int total, end, row;

    build_vlines(st, wrap_chars_for(body_w));
    total = g_nvlines;

    C2D_TargetClear(g_top, C_WHITE);
    C2D_SceneBegin(g_top);

    /* journal : du bas vers le haut */
    g_z = Z_LOG;
    end = total - v->log_scroll;
    if (end < 0) end = 0;
    if (end > total) end = total;

    row = 0;
    for (int i = end - 1; i >= 0; i--, row++) {
        const vline *l = &g_vlines[i];
        float y = (float)(TOP_H - 4 - LOG_LINE_H * (row + 1));
        if (y < -LOG_LINE_H)
            break;
        if (l->first && l->nick[0] && (l->kind == HC_MSG_CHAT || l->kind == HC_MSG_EMOTE)) {
            char nick[32];
            float col_w = (float)nick_r - 2.0f;
            float nx;
            snprintf(nick, sizeof nick, "%s", l->nick);
            while (strlen(nick) > 1 && text_width(TEXT_SCALE, nick) > col_w)
                nick[strlen(nick) - 1] = '\0';
            nx = (float)nick_r - text_width(TEXT_SCALE, nick);
            if (nx < 2.0f) nx = 2.0f;
            text_draw(nx, y, TEXT_SCALE, l->nick_color, nick);
        }
        text_draw((float)body_x, y, TEXT_SCALE, l->body_color, l->body);
    }

    /* panneau utilisateurs : fond devant le journal, contenu devant le fond */
    if (panel) {
        int count = 0;
        float y = 6.0f;
        char title[64];

        g_z = Z_PANEL_BG;
        rect(PANEL_X, 0, PANEL_W, TOP_H, C_PANEL);

        g_z = Z_PANEL_TXT;
        for (int i = 0; i < HC_MAX_USERS; i++)
            if (st->users[i].used) count++;
        snprintf(title, sizeof title, "Utilisateurs (%d)", count);
        text_draw(PANEL_X + 8.0f, y, TEXT_SCALE, C_PANEL_TXT, title);
        y += 18.0f;

        for (int i = 0; i < HC_MAX_USERS && y < TOP_H - 16; i++) {
            const hc_user *u = &st->users[i];
            char nick[32];
            if (!u->used)
                continue;
            snprintf(nick, sizeof nick, "%s", u->nick);
            while (strlen(nick) > 1 && text_width(TEXT_SCALE, nick) > (float)(PANEL_W - 34))
                nick[strlen(nick) - 1] = '\0';
            rect(PANEL_X + 8.0f, y + 2.0f, 4.0f, 8.0f, u->color);
            text_draw(PANEL_X + 18.0f, y, TEXT_SCALE, C_PANEL_TXT, nick);
            if (u->me)
                text_draw(PANEL_X + PANEL_W - 28.0f, y, TEXT_SCALE, C_GREY, "moi");
            y += 14.0f;
        }
        text_draw(PANEL_X + 8.0f, TOP_H - 14.0f, TEXT_SCALE, C_GREY, "X : fermer");
    }
    g_z = Z_LOG;
}

/* ---------- Écran du bas ---------- */

static void draw_button(float x, float y, float w, float h, uint32_t color, const char *label)
{
    float tw = text_width(TEXT_SCALE, label);
    rect(x, y, w, h, color);
    text_draw(x + (w - tw) / 2.0f, y + (h - 10.0f) / 2.0f, TEXT_SCALE, C_WHITE, label);
}

static void draw_bottom(const hc_state *st, const ui_view *v)
{
    char header[96];
    int nusers = 0;
    int connected = st->connected;

    C2D_TargetClear(g_bot, C_BG_BOT);
    C2D_SceneBegin(g_bot);
    g_z = Z_LOG;

    /* en-tête */
    rect(0, 0, BOT_W, 28, C_HEADER);
    if (st->channel[0])
        snprintf(header, sizeof header, "?%s", st->channel);
    else
        snprintf(header, sizeof header, "Aucun salon");
    text_draw(8.0f, 8.0f, 0.6f, C_TEXT, header);
    for (int i = 0; i < HC_MAX_USERS; i++)
        if (st->users[i].used) nusers++;
    {
        char info[48];
        snprintf(info, sizeof info, "%d en ligne", nusers);
        float w = text_width(TEXT_SCALE, info);
        text_draw(BOT_W - 26.0f - w, 10.0f, TEXT_SCALE, C_GREY, info);
    }
    rect(BOT_W - 16.0f, 9.0f, 8.0f, 8.0f, connected ? C_OK : C_KO);

    /* liste des salons publics */
    rect(LIST_X, LIST_Y, LIST_W, LIST_H, C_WHITE);
    rect(LIST_X, LIST_Y, LIST_W, 1, C_BORDER);
    rect(LIST_X, LIST_Y + LIST_H - 1, LIST_W, 1, C_BORDER);
    text_draw(LIST_X + 6.0f, LIST_Y + 3.0f, TEXT_SCALE, C_GREY, "Salons publics");
    if (st->nchannels == 0) {
        text_draw(LIST_X + 6.0f, LIST_Y + 20.0f, TEXT_SCALE, C_GREY,
                  "Liste vide : appuie sur Actualiser");
    }
    for (int r = 0; r < LIST_ROWS; r++) {
        int idx = v->list_offset + r;
        float y = LIST_Y + 16.0f + r * LIST_ROW_H;
        char label[72];
        if (idx >= st->nchannels)
            break;
        if (idx == st->selected_channel)
            rect(LIST_X + 1, y, LIST_W - 2, LIST_ROW_H, C_SELECT);
        snprintf(label, sizeof label, "?%s", st->channels[idx]);
        if (!strcmp(st->channels[idx], st->channel))
            snprintf(label, sizeof label, "?%s  (actuel)", st->channels[idx]);
        text_draw(LIST_X + 6.0f, y + 3.0f, TEXT_SCALE, C_TEXT, label);
    }

    /* boutons */
    draw_button(LIST_X, BTN_Y, 96, BTN_H, C_BTN, "Actualiser");
    draw_button(LIST_X + 104, BTN_Y, 96, BTN_H, st->channel[0] ? C_BTN : C_BTN_OFF,
                "Quitter salon");
    draw_button(LIST_X + 208, BTN_Y, 96, BTN_H, connected ? C_BTN_OFF : C_BTN,
                connected ? "Connecté" : "Reconnecter");

    /* zone de saisie */
    rect(LIST_X, INPUT_Y, LIST_W, INPUT_H, C_WHITE);
    rect(LIST_X, INPUT_Y, LIST_W, 1, C_BORDER);
    rect(LIST_X, INPUT_Y + INPUT_H - 1, LIST_W, 1, C_BORDER);
    text_draw(LIST_X + 8.0f, INPUT_Y + 12.0f, TEXT_SCALE, C_GREY,
              st->channel[0] ? "Écrire un message (touche A)" : "Rejoins un salon pour écrire");

    /* aide */
    text_draw(4.0f, BOT_H - 16.0f, 0.4f, C_GREY,
              "A ecrire  X utilisateurs  Y rejoindre  B quitter  START fin");
}

/* ---------- API ---------- */

void ui_init(void)
{
    static const char ref[] =
        "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_tb = C2D_TextBufNew(8192);

    /* largeur moyenne d'un caractère, pour couper le journal à la bonne largeur */
    g_char_w = text_width(TEXT_SCALE, ref) / (float)(sizeof ref - 1);
    if (g_char_w < 1.0f)
        g_char_w = 6.0f;
}

void ui_exit(void)
{
    C2D_TextBufDelete(g_tb);
}

void ui_draw(const hc_state *st, const ui_view *v)
{
    C2D_TextBufClear(g_tb);
    draw_top(st, v);
    draw_bottom(st, v);
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

ui_hit ui_hit_test(const hc_state *st, const ui_view *v, int tx, int ty)
{
    ui_hit h = { UI_HIT_NONE, 0 };

    for (int r = 0; r < LIST_ROWS; r++) {
        int y = LIST_Y + 16 + r * LIST_ROW_H;
        if (in_rect(tx, ty, LIST_X, y, LIST_W, LIST_ROW_H)) {
            int idx = v->list_offset + r;
            if (idx < st->nchannels) {
                h.kind = UI_HIT_CHANNEL;
                h.index = idx;
            }
            return h;
        }
    }
    if (in_rect(tx, ty, LIST_X, BTN_Y, 96, BTN_H)) {
        h.kind = UI_HIT_REFRESH;
    } else if (in_rect(tx, ty, LIST_X + 104, BTN_Y, 96, BTN_H)) {
        h.kind = UI_HIT_LEAVE;
    } else if (in_rect(tx, ty, LIST_X + 208, BTN_Y, 96, BTN_H)) {
        h.kind = UI_HIT_RECONNECT;
    } else if (in_rect(tx, ty, LIST_X, INPUT_Y, LIST_W, INPUT_H)) {
        h.kind = UI_HIT_INPUT;
    }
    return h;
}
