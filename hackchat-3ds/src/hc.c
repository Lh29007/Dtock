#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hc.h"

/* Échappe une chaîne pour l'insérer dans du JSON. */
static size_t json_escape(char *out, size_t cap, const char *in)
{
    size_t o = 0;
    const char *p;

    for (p = in; *p; p++) {
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
                    "{\"cmd\":\"join\",\"nick\":\"%s\",\"pass\":\"\",\"channel\":\"%s\"}",
                    n, c);
}

int hc_build_chat(char *out, size_t cap, const char *channel, const char *text)
{
    char c[256], t[1024];
    json_escape(c, sizeof c, channel);
    json_escape(t, sizeof t, text);
    return snprintf(out, cap,
                    "{\"cmd\":\"chat\",\"channel\":\"%s\",\"text\":\"%s\"}",
                    c, t);
}

/* Cherche "key": et renvoie le pointeur sur la valeur (ou NULL). */
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

/* Lit une chaîne JSON (avec décodage des échappements). */
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
                    /* ASCII seulement sur la console ; le reste devient '?'. */
                    c = (v < 0x80) ? (unsigned char)v : '?';
                    p += 4;
                } else {
                    c = '?';
                }
                break;
            }
            default:  /* \" \\ \/ */
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

static void add_user(hc_state *st, int userid, const char *nick)
{
    int i, free_slot = -1;

    for (i = 0; i < HC_MAX_USERS; i++) {
        if (st->users[i].used && st->users[i].userid == userid) {
            strncpy(st->users[i].nick, nick, sizeof st->users[i].nick - 1);
            return;
        }
        if (!st->users[i].used && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return;
    st->users[free_slot].used = 1;
    st->users[free_slot].userid = userid;
    strncpy(st->users[free_slot].nick, nick, sizeof st->users[free_slot].nick - 1);
    st->users[free_slot].nick[sizeof st->users[free_slot].nick - 1] = '\0';
}

static void remove_user(hc_state *st, int userid, char *nick_out, size_t cap)
{
    int i;
    for (i = 0; i < HC_MAX_USERS; i++) {
        if (st->users[i].used && st->users[i].userid == userid) {
            snprintf(nick_out, cap, "%s", st->users[i].nick);
            st->users[i].used = 0;
            return;
        }
    }
    snprintf(nick_out, cap, "?");
}

static const char *user_nick(const hc_state *st, int userid)
{
    int i;
    for (i = 0; i < HC_MAX_USERS; i++)
        if (st->users[i].used && st->users[i].userid == userid)
            return st->users[i].nick;
    return "?";
}

/* Parcourt le tableau "users" de onlineSet : chaque objet a un userid et un nick. */
static int parse_users(hc_state *st, const char *json)
{
    const char *p = json;
    const char *key = "\"userid\"";
    int count = 0;

    while ((p = strstr(p, key)) != NULL) {
        const char *obj_start = p;
        const char *obj_end;
        char obj[512];
        size_t len;
        int id = 0;
        char nick[32] = "";

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
            add_user(st, id, nick);
            count++;
        }
        p = obj_end + 1;
    }
    return count;
}

void hc_handle(hc_state *st, const char *json)
{
    char cmd[32] = "";
    char text[1024] = "";
    char nick[64] = "";
    char channel[64] = "";
    int userid = -1;

    if (!get_string(json, "cmd", cmd, sizeof cmd))
        return;

    if (!strcmp(cmd, "session")) {
        /* Rien à afficher : le token est géré côté serveur. */
    } else if (!strcmp(cmd, "chat")) {
        get_int(json, "userid", &userid);
        get_string(json, "text", text, sizeof text);
        printf("<%s> %s\n", user_nick(st, userid), text);
    } else if (!strcmp(cmd, "onlineSet")) {
        int n;
        get_string(json, "channel", channel, sizeof channel);
        snprintf(st->channel, sizeof st->channel, "%s", channel);
        n = parse_users(st, json);
        printf("== Salon ?%s : %d utilisateur(s) ==\n", channel, n);
    } else if (!strcmp(cmd, "onlineAdd")) {
        get_int(json, "userid", &userid);
        get_string(json, "nick", nick, sizeof nick);
        if (userid >= 0) add_user(st, userid, nick);
        printf("* %s a rejoint le salon\n", nick);
    } else if (!strcmp(cmd, "onlineRemove")) {
        char gone[32];
        get_int(json, "userid", &userid);
        remove_user(st, userid, gone, sizeof gone);
        printf("* %s a quitté le salon\n", gone);
    } else if (!strcmp(cmd, "emote")) {
        get_int(json, "userid", &userid);
        get_string(json, "text", text, sizeof text);
        printf("* %s %s\n", user_nick(st, userid), text);
    } else if (!strcmp(cmd, "info") || !strcmp(cmd, "warn")) {
        get_string(json, "text", text, sizeof text);
        printf("[%s] %s\n", cmd, text);
    } else if (!strcmp(cmd, "captcha")) {
        printf("[!] Le salon demande un captcha (non géré par ce client)\n");
    } else {
        /* Commande non gérée : affichée pour le débogage. */
        printf("(%s)\n", cmd);
    }
}
