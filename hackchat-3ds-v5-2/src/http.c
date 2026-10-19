#include <3ds.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "http.h"

#define MAX_BODY (3u * 1024u * 1024u)
#define MAX_REDIRECTS 5

static char g_err[256];

const char *http_error(void)
{
    return g_err;
}

static void set_err(const char *msg)
{
    snprintf(g_err, sizeof g_err, "%s", msg);
}

static int ps_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    if (R_FAILED(PS_GenerateRandomBytes(out, len)))
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

/* Recherche insensible à la casse de needle dans hay (longueur hlen). */
static const char *ci_find(const char *hay, size_t hlen, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;
    if (nlen == 0 || nlen > hlen)
        return NULL;
    for (i = 0; i + nlen <= hlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++)
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        if (j == nlen)
            return hay + i;
    }
    return NULL;
}

/* ---------- URL ---------- */

typedef struct {
    char host[256];
    char port[8];
    char path[HTTP_MAX_URL];
} parsed_url;

static int parse_url(const char *url, parsed_url *p)
{
    const char *s = url;
    size_t i = 0;

    memset(p, 0, sizeof *p);
    if (!strncmp(s, "https://", 8))
        s += 8;
    else if (!strncmp(s, "http://", 7))
        s += 7;   /* on passe en HTTPS : les sites http seuls échoueront */
    else
        return -1;

    while (*s && *s != '/' && *s != ':' && *s != '?') {
        if (i < sizeof p->host - 1)
            p->host[i++] = *s;
        s++;
    }
    p->host[i] = '\0';
    if (!p->host[0])
        return -1;

    if (*s == ':') {
        s++;
        i = 0;
        while (isdigit((unsigned char)*s)) {
            if (i < sizeof p->port - 1)
                p->port[i++] = *s;
            s++;
        }
        p->port[i] = '\0';
    }
    if (!p->port[0])
        snprintf(p->port, sizeof p->port, "443");

    if (*s == '/' || *s == '?')
        snprintf(p->path, sizeof p->path, "%s", s);
    else
        snprintf(p->path, sizeof p->path, "/");

    return 0;
}

/* Supprime les segments "." et ".." du chemin d'une URL absolue (query conservée). */
static void normalize_path(char *url, size_t cap)
{
    char *p = strstr(url, "://");
    char path[HTTP_MAX_URL];
    char tail[HTTP_MAX_URL];
    char result[HTTP_MAX_URL];
    char *segs[128];
    int ns = 0;
    char *ps, *q, *tok;
    size_t origin_len;

    if (!p)
        return;
    p += 3;
    ps = strchr(p, '/');
    if (!ps)
        return;
    origin_len = (size_t)(ps - url);

    snprintf(path, sizeof path, "%s", ps);
    tail[0] = '\0';
    q = strchr(path, '?');
    if (q) {
        snprintf(tail, sizeof tail, "%s", q);
        *q = '\0';
    }

    for (tok = strtok(path + 1, "/"); tok; tok = strtok(NULL, "/")) {
        if (!strcmp(tok, "."))
            continue;
        if (!strcmp(tok, "..")) {
            if (ns > 0)
                ns--;
            continue;
        }
        if (ns < (int)(sizeof segs / sizeof segs[0]))
            segs[ns++] = tok;
    }

    result[0] = '\0';
    for (int i = 0; i < ns; i++) {
        strncat(result, "/", sizeof result - strlen(result) - 1);
        strncat(result, segs[i], sizeof result - strlen(result) - 1);
    }
    if (!result[0])
        strncat(result, "/", sizeof result - 1);
    strncat(result, tail, sizeof result - strlen(result) - 1);

    {
        char origin[HTTP_MAX_URL];
        snprintf(origin, sizeof origin, "%.*s", (int)origin_len, url);
        snprintf(url, cap, "%s%s", origin, result);
    }
}

int http_resolve(const char *base, const char *href, char *out, size_t cap)
{
    char h[HTTP_MAX_URL];
    const char *scheme_end, *slash, *last;
    size_t origin_len, dir_len;

    snprintf(h, sizeof h, "%s", href);
    {
        char *hash = strchr(h, '#');
        if (hash)
            *hash = '\0';
    }
    /* retire les espaces de bord */
    {
        char *s = h;
        while (*s == ' ' || *s == '\t') s++;
        memmove(h, s, strlen(s) + 1);
    }
    if (!h[0])
        return -1;

    if (!strncmp(h, "javascript:", 11) || !strncmp(h, "mailto:", 7) ||
        !strncmp(h, "tel:", 4) || !strncmp(h, "data:", 5))
        return -1;

    if (!strncmp(h, "https://", 8) || !strncmp(h, "http://", 7)) {
        snprintf(out, cap, "%s", h);
        return 0;
    }
    if (!strncmp(h, "//", 2)) {
        snprintf(out, cap, "https:%s", h);
        return 0;
    }

    scheme_end = strstr(base, "://");
    if (!scheme_end)
        return -1;
    scheme_end += 3;
    slash = strchr(scheme_end, '/');
    origin_len = slash ? (size_t)(slash - base) : strlen(base);

    if (h[0] == '/') {
        snprintf(out, cap, "%.*s%s", (int)origin_len, base, h);
        normalize_path(out, cap);
        return 0;
    }

    if (!slash) {
        snprintf(out, cap, "%s/%s", base, h);
        normalize_path(out, cap);
        return 0;
    }

    /* URL relative : on garde le répertoire de base, puis on normalise ../ */
    last = strrchr(slash, '/');
    dir_len = (size_t)(last - base) + 1;
    snprintf(out, cap, "%.*s%s", (int)dir_len, base, h);
    normalize_path(out, cap);
    return 0;
}

/* ---------- Lecture TLS ---------- */

static int read_all(mbedtls_ssl_context *ssl, unsigned char **buf, size_t *len)
{
    size_t cap = 32768;
    *buf = malloc(cap);
    *len = 0;
    if (!*buf)
        return -1;

    for (;;) {
        int r;
        if (*len == cap) {
            unsigned char *nb;
            if (cap >= MAX_BODY)
                break;
            cap *= 2;
            if (cap > MAX_BODY)
                cap = MAX_BODY;
            nb = realloc(*buf, cap);
            if (!nb)
                return -1;
            *buf = nb;
        }
        r = mbedtls_ssl_read(ssl, *buf + *len, cap - *len);
        if (r > 0) {
            *len += (size_t)r;
        } else if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        } else {
            if (*len > 0)
                break;   /* réponse partielle : on garde ce qu'on a */
            return -1;
        }
    }
    return 0;
}

/* ---------- Décodage chunked ---------- */

static int dechunk(const unsigned char *in, size_t inlen, unsigned char **out, size_t *outlen)
{
    size_t pos = 0, olen = 0, ocap = inlen + 1;
    unsigned char *o = malloc(ocap);
    if (!o)
        return -1;

    while (pos < inlen) {
        char hex[32];
        size_t hl = 0;
        unsigned long n;
        char *end;

        while (pos < inlen && in[pos] != '\r' && hl < sizeof hex - 1)
            hex[hl++] = (char)in[pos++];
        hex[hl] = '\0';
        /* saute le reste de la ligne (extensions éventuelles) et le CRLF */
        while (pos < inlen && in[pos] != '\n')
            pos++;
        pos++;

        n = strtoul(hex, &end, 16);
        if (end == hex)
            break;
        if (n == 0)
            break;
        if (pos + n > inlen)
            n = inlen - pos;
        memcpy(o + olen, in + pos, n);
        olen += n;
        pos += n;
        /* CRLF après le chunk */
        while (pos < inlen && (in[pos] == '\r' || in[pos] == '\n'))
            pos++;
    }
    o[olen] = '\0';
    *out = o;
    *outlen = olen;
    return 0;
}

/* ---------- Une requête (sans redirection) ---------- */

static int fetch_once(const char *url, const char *ca_file, http_result *out, char *location, size_t loc_cap)
{
    parsed_url p;
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    unsigned char *raw = NULL;
    size_t rawlen = 0;
    char req[2048];
    int ret;
    int n;
    const unsigned char *hdr_end;
    const char *status_line;
    size_t hdr_len;
    int chunked;

    if (parse_url(url, &p) != 0) {
        set_err("URL invalide");
        return -1;
    }

    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    ret = -1;
    if (mbedtls_entropy_add_source(&entropy, ps_entropy, NULL, 32,
                                   MBEDTLS_ENTROPY_SOURCE_STRONG))
        { set_err("entropie"); goto cleanup; }
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)"browser3ds", 10))
        { set_err("drbg"); goto cleanup; }
    if (mbedtls_x509_crt_parse_file(&cacert, ca_file) < 0)
        { set_err("certificats racine introuvables"); goto cleanup; }

    if (mbedtls_net_connect(&net, p.host, p.port, MBEDTLS_NET_PROTO_TCP)) {
        set_err("connexion impossible (DNS / réseau)");
        goto cleanup;
    }
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT)) {
        set_err("config TLS");
        goto cleanup;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &conf) || mbedtls_ssl_set_hostname(&ssl, p.host)) {
        set_err("setup TLS");
        goto cleanup;
    }
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);

    do {
        ret = mbedtls_ssl_handshake(&ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret) {
        set_err("TLS : certificat ou protocole refusé par la 3DS");
        ret = -1;
        goto cleanup;
    }

    n = snprintf(req, sizeof req,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: Mozilla/5.0 (Nintendo 3DS; Browser3DS/1.0)\r\n"
                 "Accept: text/html,text/plain;q=0.9,*/*;q=0.5\r\n"
                 "Accept-Encoding: identity\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 p.path, p.host);
    if (n <= 0 || (size_t)n >= sizeof req) {
        set_err("requête trop longue");
        ret = -1;
        goto cleanup;
    }

    {
        int sent = 0;
        while (sent < n) {
            int w = mbedtls_ssl_write(&ssl, (const unsigned char *)req + sent, (size_t)(n - sent));
            if (w > 0)
                sent += w;
            else if (w != MBEDTLS_ERR_SSL_WANT_WRITE && w != MBEDTLS_ERR_SSL_WANT_READ) {
                set_err("envoi de la requête");
                ret = -1;
                goto cleanup;
            }
        }
    }

    if (read_all(&ssl, &raw, &rawlen) != 0 || rawlen == 0) {
        set_err("aucune réponse du serveur");
        ret = -1;
        goto cleanup;
    }

    mbedtls_ssl_close_notify(&ssl);

    /* En-tête HTTP */
    hdr_end = NULL;
    for (size_t i = 0; i + 4 <= rawlen; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            hdr_end = raw + i;
            break;
        }
    }
    if (!hdr_end) {
        set_err("réponse HTTP mal formée");
        ret = -1;
        goto cleanup;
    }
    hdr_len = (size_t)(hdr_end - raw);

    status_line = (const char *)raw;
    out->status = 0;
    if (!strncmp(status_line, "HTTP/", 5)) {
        const char *sp = strchr(status_line, ' ');
        if (sp)
            out->status = atoi(sp + 1);
    }

    location[0] = '\0';
    {
        const char *loc = ci_find((const char *)raw, hdr_len, "\nlocation:");
        if (loc) {
            const char *v = loc + 10;
            size_t l = 0;
            while (*v == ' ' || *v == '\t') v++;
            while (v[l] && v[l] != '\r' && v[l] != '\n' && l < loc_cap - 1) l++;
            memcpy(location, v, l);
            location[l] = '\0';
        }
    }

    chunked = ci_find((const char *)raw, hdr_len, "\ntransfer-encoding:") != NULL &&
              ci_find((const char *)raw, hdr_len, "chunked") != NULL;

    {
        const unsigned char *body = hdr_end + 4;
        size_t blen = rawlen - (size_t)(body - raw);
        if (chunked) {
            unsigned char *dec = NULL;
            size_t dlen = 0;
            if (dechunk(body, blen, &dec, &dlen) != 0) {
                set_err("décodage chunked");
                ret = -1;
                goto cleanup;
            }
            out->body = dec;
            out->len = dlen;
        } else {
            out->body = malloc(blen + 1);
            if (!out->body) {
                set_err("mémoire insuffisante");
                ret = -1;
                goto cleanup;
            }
            memcpy(out->body, body, blen);
            out->body[blen] = '\0';
            out->len = blen;
        }
    }

    ret = 0;

cleanup:
    free(raw);
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

/* ---------- API publique ---------- */

int http_get(const char *url, const char *ca_file, http_result *out)
{
    char cur[HTTP_MAX_URL];
    int hops;

    memset(out, 0, sizeof *out);
    snprintf(cur, sizeof cur, "%s", url);

    for (hops = 0; hops <= MAX_REDIRECTS; hops++) {
        char loc[HTTP_MAX_URL];
        char next[HTTP_MAX_URL];
        int rc;

        http_result_free(out);
        memset(out, 0, sizeof *out);
        rc = fetch_once(cur, ca_file, out, loc, sizeof loc);
        if (rc != 0)
            return -1;

        if (out->status >= 301 && out->status <= 308 && loc[0] &&
            hops < MAX_REDIRECTS) {
            if (http_resolve(cur, loc, next, sizeof next) != 0) {
                set_err("redirection vers une URL non supportée");
                return -1;
            }
            snprintf(cur, sizeof cur, "%s", next);
            continue;
        }

        snprintf(out->final_url, sizeof out->final_url, "%s", cur);
        return 0;
    }

    set_err("trop de redirections");
    return -1;
}

void http_result_free(http_result *r)
{
    free(r->body);
    r->body = NULL;
    r->len = 0;
}
