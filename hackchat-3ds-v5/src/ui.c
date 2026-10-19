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

#define LOG_LINE_H     14
#define LOG_NICK_X_R   96           /* panneau fermé : colonne des pseudos */
#define LOG_BODY_X     104
#define LOG_BODY_W     (TOP_W - LOG_BODY_X - 6)
#define LOG_NICK_X_R_P 60           /* panneau ouvert : journal réduit à gauche */
#define LOG_BODY_X_P   66
#define LOG_BODY_W_P   (PANEL_X - LOG_BODY_X_P - 6)
#define TEXT_SCALE     0.5f
#define MAX_VLINES     2000
#define PANEL_X        224
#define PANEL_W        (TOP_W - PANEL_X)
#define LIST_ROWS      UI_LIST_ROWS
#define LIST_ROW_H     18
#define LIST_X         8
#define LIST_Y         36
#define LIST_W         304
#define LIST_H         (LIST_ROWS * LIST_ROW_H + 18)
#define BTN_Y          (LIST_Y + LIST_H + 6)
#define BTN_H          26
#define INPUT_Y        (BTN_Y + BTN_H + 8)
#define INPUT_H        38

/* Bouton de l'en-tête (Réglages / Retour) */
#define HDR_BTN_X      228
#define HDR_BTN_Y      4
#define HDR_BTN_W      84
#define HDR_BTN_H      20

/* Écran Réglages */
#define SET_ROW_X      LIST_X
#define SET_ROW_W      LIST_W
#define SET_NICK_Y     36
#define SET_THEME_Y    70
#define SET_ROW_H      30
#define SET_USERS_Y    106
#define SET_USER_ROW_H 14
#define SET_BACK_Y     210
#define SET_BACK_H     24

/* Profondeur citro2d : plus la valeur est grande, plus l'élément est au premier plan. */
#define Z_LOG       0.5f
#define Z_PANEL_BG  0.8f
#define Z_PANEL_TXT 0.9f

#define RGBA8(r, g, b) ((uint32_t)(0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))

/* Couleurs fixes (boutons, panneau, pastilles) */
#define C_BTN       RGBA8(0x3C, 0x7D, 0xD8)
#define C_BTN_OFF   RGBA8(0x9A, 0x9A, 0x9A)
#define C_PANEL     RGBA8(0x2B, 0x2B, 0x2B)
#define C_PANEL_TXT RGBA8(0xE6, 0xE6, 0xE6)
#define C_PANEL_GREY RGBA8(0x9A, 0x9A, 0x9A)
#define C_WHITE     RGBA8(0xFF, 0xFF, 0xFF)
#define C_OK        RGBA8(0x2E, 0xB8, 0x4B)
#define C_KO        RGBA8(0xD6, 0x3B, 0x3B)

/* Palette selon le thème */
typedef struct {
    uint32_t log_bg;     /* fond du journal (écran du haut) */
    uint32_t app_bg;     /* fond de l'écran du bas */
    uint32_t header;     /* bandeau d'en-tête */
    uint32_t surface;    /* listes, zones de saisie, lignes de réglages */
    uint32_t border;
    uint32_t select;     /* ligne sélectionnée */
    uint32_t text;
    uint32_t grey;
} palette;

static const palette PAL_LIGHT = {
    RGBA8(0xFF, 0xFF, 0xFF), RGBA8(0xF2, 0xF2, 0xF2), RGBA8(0xDA, 0xDA, 0xDA),
    RGBA8(0xFF, 0xFF, 0xFF), RGBA8(0xB8, 0xB8, 0xB8), RGBA8(0xCC, 0xE0, 0xFF),
    RGBA8(0x1A, 0x1A, 0x1A), RGBA8(0x80, 0x80, 0x80),
};

static const palette PAL_DARK = {
    RGBA8(0x1C, 0x1C, 0x1E), RGBA8(0x12, 0x12, 0x14), RGBA8(0x2C, 0x2C, 0x2E),
    RGBA8(0x24, 0x24, 0x26), RGBA8(0x3A, 0x3A, 0x3C), RGBA8(0x2F, 0x4A, 0x6E),
    RGBA8(0xE8, 0xE8, 0xE8), RGBA8(0x9A, 0x9A, 0x9A),
};

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
static const palette *g_pal = &PAL_LIGHT;
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

/* Largeur estimée d'un pseudo, sans mesure de texte (chaque mesure consomme des glyphes du buffer). */
static float nick_width(const char *s)
{
    return (float)strlen(s) * g_char_w;
}

/* Pseudo tronqué pour tenir dans max_w pixels (estimation par caractères). */
static void fit_nick(char *nick, size_t cap, const char *src, float max_w)
{
    size_t max_chars;
    snprintf(nick, cap, "%s", src);
    max_chars = (size_t)(max_w / g_char_w);
    if (max_chars < 1)
        max_chars = 1;
    if (strlen(nick) > max_chars)
        nick[max_chars] = '\0';
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
        v.body_color = g_pal->text;
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
                        m->kind == HC_MSG_LEAVE) ? g_pal->grey : g_pal->text;

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

static int count_users(const hc_state *st)
{
    int n = 0;
    for (int i = 0; i < HC_MAX_USERS; i++)
        if (st->users[i].used) n++;
    return n;
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

    C2D_TargetClear(g_top, g_pal->log_bg);
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
            float nx;
            fit_nick(nick, sizeof nick, l->nick, (float)nick_r - 2.0f);
            nx = (float)nick_r - nick_width(nick);
            if (nx < 2.0f) nx = 2.0f;
            text_draw(nx, y, TEXT_SCALE, l->nick_color, nick);
        }
        text_draw((float)body_x, y, TEXT_SCALE, l->body_color, l->body);
    }

    /* panneau utilisateurs : fond devant le journal, contenu devant le fond */
    if (panel) {
        float y = 6.0f;
        char title[64];

        g_z = Z_PANEL_BG;
        rect(PANEL_X, 0, PANEL_W, TOP_H, C_PANEL);

        g_z = Z_PANEL_TXT;
        snprintf(title, sizeof title, "Utilisateurs (%d)", count_users(st));
        text_draw(PANEL_X + 8.0f, y, TEXT_SCALE, C_PANEL_TXT, title);
        y += 18.0f;

        for (int i = 0; i < HC_MAX_USERS && y < TOP_H - 16; i++) {
            const hc_user *u = &st->users[i];
            char nick[32];
            if (!u->used)
                continue;
            fit_nick(nick, sizeof nick, u->nick, (float)(PANEL_W - 52));
            rect(PANEL_X + 8.0f, y + 2.0f, 4.0f, 8.0f, u->color);
            text_draw(PANEL_X + 18.0f, y, TEXT_SCALE, C_PANEL_TXT, nick);
            if (u->me)
                text_draw(PANEL_X + PANEL_W - 28.0f, y, TEXT_SCALE, C_PANEL_GREY, "moi");
            y += 14.0f;
        }
        text_draw(PANEL_X + 8.0f, TOP_H - 14.0f, TEXT_SCALE, C_PANEL_GREY, "X : fermer");
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

static void draw_header(const hc_state *st, const ui_view *v)
{
    char header[96];
    char info[48];
    float w;

    rect(0, 0, BOT_W, 28, g_pal->header);
    if (v->screen == UI_SCREEN_SETTINGS) {
        text_draw(8.0f, 8.0f, 0.6f, g_pal->text, "Réglages");
    } else {
        if (st->channel[0])
            snprintf(header, sizeof header, "?%s", st->channel);
        else
            snprintf(header, sizeof header, "Aucun salon");
        text_draw(8.0f, 8.0f, 0.6f, g_pal->text, header);

        snprintf(info, sizeof info, "%d en ligne", count_users(st));
        w = text_width(TEXT_SCALE, info);
        text_draw(HDR_BTN_X - 34.0f - w, 10.0f, TEXT_SCALE, g_pal->grey, info);
        rect(HDR_BTN_X - 20.0f, 9.0f, 8.0f, 8.0f, st->connected ? C_OK : C_KO);
    }

    if (v->screen == UI_SCREEN_SETTINGS)
        draw_button(HDR_BTN_X, HDR_BTN_Y, HDR_BTN_W, HDR_BTN_H, C_BTN_OFF, "Retour");
    else
        draw_button(HDR_BTN_X, HDR_BTN_Y, HDR_BTN_W, HDR_BTN_H, C_BTN, "Réglages");
}

static void draw_chat(const hc_state *st, const ui_view *v)
{
    int connected = st->connected;

    /* liste des salons publics */
    rect(LIST_X, LIST_Y, LIST_W, LIST_H, g_pal->surface);
    rect(LIST_X, LIST_Y, LIST_W, 1, g_pal->border);
    rect(LIST_X, LIST_Y + LIST_H - 1, LIST_W, 1, g_pal->border);
    text_draw(LIST_X + 6.0f, LIST_Y + 3.0f, TEXT_SCALE, g_pal->grey, "Salons publics");
    if (st->nchannels == 0) {
        text_draw(LIST_X + 6.0f, LIST_Y + 20.0f, TEXT_SCALE, g_pal->grey,
                  "Liste vide : appuie sur Actualiser");
    }
    for (int r = 0; r < LIST_ROWS; r++) {
        int idx = v->list_offset + r;
        float y = LIST_Y + 16.0f + r * LIST_ROW_H;
        char label[72];
        if (idx >= st->nchannels)
            break;
        if (idx == st->selected_channel)
            rect(LIST_X + 1, y, LIST_W - 2, LIST_ROW_H, g_pal->select);
        snprintf(label, sizeof label, "?%s", st->channels[idx]);
        if (!strcmp(st->channels[idx], st->channel))
            snprintf(label, sizeof label, "?%s  (actuel)", st->channels[idx]);
        text_draw(LIST_X + 6.0f, y + 3.0f, TEXT_SCALE, g_pal->text, label);
    }

    /* boutons */
    draw_button(LIST_X, BTN_Y, 96, BTN_H, C_BTN, "Actualiser");
    draw_button(LIST_X + 104, BTN_Y, 96, BTN_H, st->channel[0] ? C_BTN : C_BTN_OFF,
                "Quitter salon");
    draw_button(LIST_X + 208, BTN_Y, 96, BTN_H, connected ? C_BTN_OFF : C_BTN,
                connected ? "Connecté" : "Reconnecter");

    /* zone de saisie */
    rect(LIST_X, INPUT_Y, LIST_W, INPUT_H, g_pal->surface);
    rect(LIST_X, INPUT_Y, LIST_W, 1, g_pal->border);
    rect(LIST_X, INPUT_Y + INPUT_H - 1, LIST_W, 1, g_pal->border);
    text_draw(LIST_X + 8.0f, INPUT_Y + 12.0f, TEXT_SCALE, g_pal->grey,
              st->channel[0] ? "Écrire un message (touche A)" : "Rejoins un salon pour écrire");

    /* aide */
    text_draw(4.0f, BOT_H - 16.0f, 0.4f, g_pal->grey,
              "A ecrire  X utilisateurs  Y rejoindre  B quitter  SELECT reglages  START fin");
}

static void draw_settings(const hc_state *st)
{
    char line[64];
    int count = count_users(st);
    float y = SET_USERS_Y + 14.0f;
    int shown = 0;

    /* pseudo */
    rect(SET_ROW_X, SET_NICK_Y, SET_ROW_W, SET_ROW_H, g_pal->surface);
    rect(SET_ROW_X, SET_NICK_Y + SET_ROW_H - 1, SET_ROW_W, 1, g_pal->border);
    text_draw(SET_ROW_X + 8.0f, SET_NICK_Y + 3.0f, TEXT_SCALE, g_pal->grey, "Pseudo");
    snprintf(line, sizeof line, "%s", st->nick);
    text_draw(SET_ROW_X + 8.0f, SET_NICK_Y + 15.0f, TEXT_SCALE, g_pal->text, line);
    text_draw(SET_ROW_X + SET_ROW_W - 80.0f, SET_NICK_Y + 9.0f, TEXT_SCALE, C_BTN, "Changer >");

    /* thème */
    rect(SET_ROW_X, SET_THEME_Y, SET_ROW_W, SET_ROW_H, g_pal->surface);
    rect(SET_ROW_X, SET_THEME_Y + SET_ROW_H - 1, SET_ROW_W, 1, g_pal->border);
    text_draw(SET_ROW_X + 8.0f, SET_THEME_Y + 3.0f, TEXT_SCALE, g_pal->grey, "Thème");
    text_draw(SET_ROW_X + 8.0f, SET_THEME_Y + 15.0f, TEXT_SCALE, g_pal->text,
              g_pal == &PAL_DARK ? "Sombre" : "Clair");
    text_draw(SET_ROW_X + SET_ROW_W - 110.0f, SET_THEME_Y + 9.0f, TEXT_SCALE, C_BTN,
              "Basculer >");

    /* personnes dans le salon */
    snprintf(line, sizeof line, "Personnes dans le salon (%d)", count);
    text_draw(SET_ROW_X, SET_USERS_Y, TEXT_SCALE, g_pal->grey, line);
    for (int i = 0; i < HC_MAX_USERS && shown < UI_USER_ROWS; i++) {
        const hc_user *u = &st->users[i];
        char nick[32];
        if (!u->used)
            continue;
        fit_nick(nick, sizeof nick, u->nick, (float)(SET_ROW_W - 60));
        rect(SET_ROW_X + 2.0f, y + 2.0f, 4.0f, 8.0f, u->color);
        text_draw(SET_ROW_X + 12.0f, y, TEXT_SCALE, g_pal->text, nick);
        if (u->me)
            text_draw(SET_ROW_X + SET_ROW_W - 40.0f, y, TEXT_SCALE, g_pal->grey, "(moi)");
        y += SET_USER_ROW_H;
        shown++;
    }
    if (count == 0)
        text_draw(SET_ROW_X + 12.0f, y, TEXT_SCALE, g_pal->grey, "Rejoins un salon pour voir les gens");

    /* retour */
    draw_button(SET_ROW_X, SET_BACK_Y, SET_ROW_W, SET_BACK_H, C_BTN, "Retour au salon");
}

static void draw_bottom(const hc_state *st, const ui_view *v)
{
    C2D_TargetClear(g_bot, g_pal->app_bg);
    C2D_SceneBegin(g_bot);
    g_z = Z_LOG;

    draw_header(st, v);
    if (v->screen == UI_SCREEN_SETTINGS)
        draw_settings(st);
    else
        draw_chat(st, v);
}

/* ---------- API ---------- */

void ui_init(void)
{
    static const char ref[] =
        "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    /* nombre de glyphes (pas d'octets) gardés par frame : marge large */
    g_tb = C2D_TextBufNew(16384);

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
    g_pal = v->dark ? &PAL_DARK : &PAL_LIGHT;
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

    /* bouton de l'en-tête, présent sur les deux écrans */
    if (in_rect(tx, ty, HDR_BTN_X, HDR_BTN_Y, HDR_BTN_W, HDR_BTN_H)) {
        h.kind = (v->screen == UI_SCREEN_SETTINGS) ? UI_HIT_BACK : UI_HIT_SETTINGS;
        return h;
    }

    if (v->screen == UI_SCREEN_SETTINGS) {
        if (in_rect(tx, ty, SET_ROW_X, SET_NICK_Y, SET_ROW_W, SET_ROW_H))
            h.kind = UI_HIT_NICK;
        else if (in_rect(tx, ty, SET_ROW_X, SET_THEME_Y, SET_ROW_W, SET_ROW_H))
            h.kind = UI_HIT_THEME;
        else if (in_rect(tx, ty, SET_ROW_X, SET_BACK_Y, SET_ROW_W, SET_BACK_H))
            h.kind = UI_HIT_BACK;
        return h;
    }

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
