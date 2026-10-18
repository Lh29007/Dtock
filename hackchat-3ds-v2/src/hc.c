#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hc.h"

/* Couleur RGBA au format citro2d : C2D_Color32 = 0xAABBGGRR. */
#define RGBA8(r, g, b) ((uint32_t)(0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))

#define COLOR_DEFAULT_NICK RGBA8(0x55, 0x55, 0x55)
#define COLOR_INFO         RGBA8(0x88, 0x88, 0x88)

/* ---------- Écriture JSON ---------- */

static size_t json_escape(char *out, size_t cap, const char *in)
{
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char tmp[8];
        const char *rep = NULL;
        size_t l;

        switch (c) {
        case '"':  rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        default:
            if (c < 0x20) {
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                rep = tmp;
            }
        }
        if (rep) {
            l = strlen(rep);
            if (o + l >= cap) break;
            memcpy(out + o, rep, l);
            o += l;
        } else {
            if (o + 1 >= cap) break;
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
    return o;
}

int hc_build_join(char *out, size_t cap, const char *nick, const char *channel)
{
    char n[128], c[256];
    json_escape(n, sizeof n, nick);
    json_escape(c, sizeof c, channel);
    return snprintf(out, cap,
                    "{\"cmd\":\"join\",\"nick\":\"%s\",\"pass\":\"\",\"channel\":\"%s\"}", n, c);
}

int hc_build_chat(char *out, size_t cap, const char *channel, const char *text)
{
    char c[256], t[1024];
    json_escape(c, sizeof c, channel);
    json_escape(t, sizeof t, text);
    return snprintf(out, cap, "{\"cmd\":\"chat\",\"channel\":\"%s\",\"text\":\"%s\"}", c, t);
}

int hc_build_leave(char *out, size_t cap, const char *channel)
{
    char c[256];
    json_escape(c, sizeof c, channel);
    return snprintf(out, cap, "{\"cmd\":\"leave\",\"channel\":\"%s\"}", c);
}

int hc_build_getchannels(char *out, size_t cap)
{
    return snprintf(out, cap, "{\"cmd\":\"getchannels\"}");
}

/* ---------- Lecture JSON (minimaliste, messages à plat) ---------- */

static const char *find_value(const char *json, const char *key)
{
    char pat[80];
    const char *p;

    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(json, pat);
    if (!p)
        return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int get_string(const char *json, const char *key, char *out, size_t cap)
{
    const char *p = find_value(json, key);
    size_t o = 0;

    if (!p || *p != '"')
        return 0;
    p++;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b':
            case 'f': c = ' '; break;
            case 'u': {
                unsigned v = 0;
                int ok = 1, i;
                for (i = 1; i <= 4; i++) {
                    char h = p[i];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v += (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v += (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v += (unsigned)(h - 'A' + 10);
                    else { ok = 0; break; }
                }
                if (ok) {
                    c = (v < 0x80) ? (unsigned char)v : '?';
                    p += 4;
                } else {
                    c = '?';
                }
                break;
            }
            default:
                c = (unsigned char)*p;
                break;
            }
        }
        if (o + 1 >= cap)
            break;
        out[o++] = (char)c;
        p++;
    }
    out[o] = '\0';
    return 1;
}

static int get_int(const char *json, const char *key, int *val)
{
    const char *p = find_value(json, key);
    char *end;
    long v;

    if (!p)
        return 0;
    v = strtol(p, &end, 10);
    if (end == p)
        return 0;
    *val = (int)v;
    return 1;
}

static int get_bool_true(const char *json, const char *key)
{
    const char *p = find_value(json, key);
    return p && !strncmp(p, "true", 4);
}

/* "CCCCCC" ou "#cccccc" -> couleur ; sinon couleur par défaut. */
static uint32_t parse_color(const char *s, uint32_t dflt)
{
    unsigned long v;
    char *end;

    if (!s || !*s)
        return dflt;
    if (*s == '#')
        s++;
    if (strlen(s) != 6)
        return dflt;
    v = strtoul(s, &end, 16);
    if (*end != '\0')
        return dflt;
    return RGBA8((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

/* ---------- État ---------- */

void hc_init(hc_state *st, const char *nick)
{
    memset(st, 0, sizeof *st);
    snprintf(st->nick, sizeof st->nick, "%s", nick);
    st->my_id = -1;
    st->selected_channel = 0;
}

void hc_clear_users(hc_state *st)
{
    memset(st->users, 0, sizeof st->users);
}

static void log_push(hc_state *st, hc_msg_kind kind, const char *nick, uint32_t color, const char *text)
{
    hc_msg *m;

    if (st->nlog == HC_MAX_LOG) {
        memmove(st->log, st->log + 1, sizeof(hc_msg) * (HC_MAX_LOG - 1));
        st->nlog--;
    }
    m = &st->log[st->nlog++];
    m->kind = kind;
    snprintf(m->nick, sizeof m->nick, "%s", nick ? nick : "");
    m->color = color;
    snprintf(m->text, sizeof m->text, "%s", text ? text : "");
}

void hc_log_info(hc_state *st, const char *text)
{
    log_push(st, HC_MSG_INFO, "", COLOR_INFO, text);
}

static hc_user *find_user(hc_state *st, int userid)
{
    for (int i = 0; i < HC_MAX_USERS; i++)
        if (st->users[i].used && st->users[i].userid == userid)
            return &st->users[i];
    return NULL;
}

static void add_user(hc_state *st, int userid, const char *nick, uint32_t color, int me)
{
    hc_user *u = find_user(st, userid);
    if (!u) {
        for (int i = 0; i < HC_MAX_USERS; i++) {
            if (!st->users[i].used) {
                u = &st->users[i];
                break;
            }
        }
    }
    if (!u)
        return;
    u->used = 1;
    u->userid = userid;
    u->me = me;
    snprintf(u->nick, sizeof u->nick, "%s", nick ? nick : "anonymous");
    u->color = color;
    if (me)
        st->my_id = userid;
}

static const char *nick_of(hc_state *st, int userid, uint32_t *color)
{
    hc_user *u = find_user(st, userid);
    if (u) {
        if (color) *color = u->color;
        return u->nick;
    }
    if (color) *color = COLOR_DEFAULT_NICK;
    return "?";
}

/* Parcourt le tableau "users" de onlineSet : objets avec userid, nick, color, isme. */
static void parse_users(hc_state *st, const char *json)
{
    const char *p = json;
    const char *key = "\"userid\"";

    while ((p = strstr(p, key)) != NULL) {
        const char *obj_start = p;
        const char *obj_end;
        char obj[512];
        size_t len;
        int id = 0;
        char nick[32] = "";
        char color[16] = "";

        while (obj_start > json && *obj_start != '{')
            obj_start--;
        obj_end = strchr(p, '}');
        if (!obj_end)
            break;

        len = (size_t)(obj_end - obj_start) + 1;
        if (len >= sizeof obj)
            len = sizeof obj - 1;
        memcpy(obj, obj_start, len);
        obj[len] = '\0';

        if (get_int(obj, "userid", &id)) {
            get_string(obj, "nick", nick, sizeof nick);
            get_string(obj, "color", color, sizeof color);
            add_user(st, id, nick, parse_color(color, COLOR_DEFAULT_NICK),
                     get_bool_true(obj, "isme"));
        }
        p = obj_end + 1;
    }
}

static void parse_channels(hc_state *st, const char *json)
{
    const char *p = find_value(json, "list");
    st->nchannels = 0;
    if (!p || *p != '[')
        return;
    p++;
    while (*p && *p != ']' && st->nchannels < HC_MAX_CHANNELS) {
        if (*p == '"') {
            char name[64];
            size_t o = 0;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                if (o + 1 < sizeof name) name[o++] = *p;
                p++;
            }
            name[o] = '\0';
            if (o > 0)
                snprintf(st->channels[st->nchannels++], 64, "%s", name);
        }
        if (*p == '"') p++;
        while (*p && *p != '"' && *p != ']') p++;
    }
}

void hc_handle(hc_state *st, const char *json)
{
    char cmd[32] = "";
    char text[HC_TEXT_MAX] = "";
    char nick[32] = "";
    char color[16] = "";
    char channel[64] = "";
    int userid = -1;

    if (!get_string(json, "cmd", cmd, sizeof cmd))
        return;

    if (!strcmp(cmd, "chat")) {
        uint32_t col;
        get_int(json, "userid", &userid);
        get_string(json, "text", text, sizeof text);
        const char *who = nick_of(st, userid, &col);
        log_push(st, HC_MSG_CHAT, who, col, text);

    } else if (!strcmp(cmd, "onlineSet")) {
        get_string(json, "channel", channel, sizeof channel);
        snprintf(st->channel, sizeof st->channel, "%s", channel);
        hc_clear_users(st);
        parse_users(st, json);
        {
            char info[128];
            snprintf(info, sizeof info, "Connecté au salon ?%s", channel);
            hc_log_info(st, info);
        }

    } else if (!strcmp(cmd, "onlineAdd")) {
        get_int(json, "userid", &userid);
        get_string(json, "nick", nick, sizeof nick);
        get_string(json, "color", color, sizeof color);
        add_user(st, userid, nick, parse_color(color, COLOR_DEFAULT_NICK), 0);
        log_push(st, HC_MSG_JOIN, nick, COLOR_INFO, "a rejoint le salon");

    } else if (!strcmp(cmd, "onlineRemove")) {
        hc_user *u;
        get_int(json, "userid", &userid);
        u = find_user(st, userid);
        if (u) {
            snprintf(nick, sizeof nick, "%s", u->nick);
            u->used = 0;
            log_push(st, HC_MSG_LEAVE, nick, COLOR_INFO, "a quitté le salon");
        }

    } else if (!strcmp(cmd, "publicchannels")) {
        parse_channels(st, json);
        if (st->selected_channel >= st->nchannels)
            st->selected_channel = 0;

    } else if (!strcmp(cmd, "emote")) {
        uint32_t col;
        get_int(json, "userid", &userid);
        get_string(json, "text", text, sizeof text);
        log_push(st, HC_MSG_EMOTE, nick_of(st, userid, &col), col, text);

    } else if (!strcmp(cmd, "info") || !strcmp(cmd, "warn")) {
        get_string(json, "text", text, sizeof text);
        hc_log_info(st, text);

    } else if (!strcmp(cmd, "captcha")) {
        hc_log_info(st, "Le salon demande un captcha (non géré).");

    } else if (!strcmp(cmd, "session")) {
        st->connected = 1;
    }
}
