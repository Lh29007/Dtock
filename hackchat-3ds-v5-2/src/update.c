#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "http.h"
#include "update.h"

/* Lit la chaîne JSON qui suit key (ex. "\"tag_name\"") dans p. Retourne 1 si trouvée. */
static int json_string_after(const char *p, const char *key, char *out, size_t cap)
{
    const char *v = strstr(p, key);
    size_t n = 0;

    if (!v)
        return 0;
    v += strlen(key);
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n' || *v == ':')
        v++;
    if (*v != '"')
        return 0;
    v++;
    while (*v && *v != '"' && n + 1 < cap)
        out[n++] = *v++;
    out[n] = '\0';
    return *v == '"';
}

/* Première release dont le tag commence par UPDATE_TAG et qui a un asset .3dsx. */
static int find_latest(const char *json, char *url, size_t ucap, char *tag, size_t tcap)
{
    const char *p = json;

    while ((p = strstr(p, "\"tag_name\"")) != NULL) {
        const char *next = strstr(p + 1, "\"tag_name\"");
        const char *a = p;
        char t[64];

        if (json_string_after(p, "\"tag_name\"", t, sizeof t) &&
            !strncmp(t, UPDATE_TAG, strlen(UPDATE_TAG))) {
            while ((a = strstr(a, "\"browser_download_url\"")) != NULL &&
                   (next == NULL || a < next)) {
                char u[HTTP_MAX_URL];
                if (json_string_after(a, "\"browser_download_url\"", u, sizeof u)) {
                    size_t ul = strlen(u);
                    if (ul > 6 && !strcmp(u + ul - 6, ".3dsx")) {
                        snprintf(url, ucap, "%s", u);
                        snprintf(tag, tcap, "%s", t);
                        return 0;
                    }
                }
                a++;
            }
        }
        p++;
    }
    return -1;
}

static void say(update_log_fn log, void *ctx, const char *fmt, const char *arg)
{
    char line[160];
    if (!log)
        return;
    if (arg)
        snprintf(line, sizeof line, fmt, arg);
    else
        snprintf(line, sizeof line, "%s", fmt);
    log(line, ctx);
}

int update_run(update_log_fn log, void *ctx)
{
    http_result api;
    http_result bin;
    char url[HTTP_MAX_URL];
    char tag[64];
    char tmp[256];
    FILE *f;

    memset(&api, 0, sizeof api);
    memset(&bin, 0, sizeof bin);

    say(log, ctx, "Recherche de la dernière version...", NULL);
    if (http_get(UPDATE_API_URL, UPDATE_CA, &api) != 0) {
        say(log, ctx, "Pas de connexion : %s", http_error());
        http_result_free(&api);
        return -1;
    }
    if (api.status != 200) {
        char code[64];
        snprintf(code, sizeof code, "HTTP %d", api.status);
        say(log, ctx, "GitHub a répondu : %s", code);
        http_result_free(&api);
        return -1;
    }
    if (find_latest((const char *)api.body, url, sizeof url, tag, sizeof tag) != 0) {
        say(log, ctx, "Aucune version trouvée sur GitHub.", NULL);
        http_result_free(&api);
        return -1;
    }
    http_result_free(&api);
    say(log, ctx, "Version trouvée : %s", tag);

    say(log, ctx, "Téléchargement...", NULL);
    if (http_get(url, UPDATE_CA, &bin) != 0 || bin.status != 200 ||
        bin.len < 4 || memcmp(bin.body, "3DSX", 4) != 0) {
        say(log, ctx, "Téléchargement échoué.", NULL);
        http_result_free(&bin);
        return -1;
    }

    mkdir("sdmc:/3ds", 0777);
    mkdir(UPDATE_DIR, 0777);
    snprintf(tmp, sizeof tmp, "%s.part", UPDATE_FILE);
    f = fopen(tmp, "wb");
    if (!f) {
        say(log, ctx, "Impossible d'écrire sur la carte SD.", NULL);
        http_result_free(&bin);
        return -1;
    }
    if (fwrite(bin.body, 1, bin.len, f) != bin.len) {
        fclose(f);
        remove(tmp);
        say(log, ctx, "Écriture sur la carte SD échouée.", NULL);
        http_result_free(&bin);
        return -1;
    }
    fclose(f);
    http_result_free(&bin);

    remove(UPDATE_FILE);
    if (rename(tmp, UPDATE_FILE) != 0) {
        say(log, ctx, "Renommage du fichier échoué.", NULL);
        return -1;
    }
    say(log, ctx, "Installé dans sdmc:/3ds/hackchat/", NULL);
    say(log, ctx, "Relance hack.chat V5 depuis le Homebrew Launcher.", NULL);
    return 0;
}
