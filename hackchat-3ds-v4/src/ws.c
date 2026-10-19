#include <3ds.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "ws.h"

#define WS_MAX_SEND 1024
#define WS_MAX_FRAME (1024 * 1024)

struct ws_conn {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
};

/* Source d'entropie : le générateur matériel de la console. */
static int ps_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    if (R_FAILED(PS_GenerateRandomBytes(out, len)))
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

static void conn_free(ws_conn *c)
{
    mbedtls_net_free(&c->net);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_x509_crt_free(&c->cacert);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
}

/* Écrit tout le buffer (gère WANT_READ / WANT_WRITE). */
static int ws_write_all(ws_conn *c, const unsigned char *d, size_t n)
{
    size_t off = 0;
    while (off < n) {
        int r = mbedtls_ssl_write(&c->ssl, d + off, n - off);
        if (r > 0) {
            off += (size_t)r;
        } else if (r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ) {
            svcSleepThread(1000000LL);
        } else {
            return -1;
        }
    }
    return 0;
}

/* Envoie une trame client (toujours masquée, RFC 6455). */
static int ws_send_frame(ws_conn *c, int opcode, const unsigned char *payload, size_t len)
{
    unsigned char hdr[8];
    unsigned char mask[4];
    unsigned char *masked;
    size_t h = 0;
    int rc;

    if (len > 0xFFFF)
        return -1;

    mbedtls_ctr_drbg_random(&c->drbg, mask, sizeof mask);

    hdr[h++] = (unsigned char)(0x80 | opcode);
    if (len < 126) {
        hdr[h++] = (unsigned char)(0x80 | len);
    } else {
        hdr[h++] = (unsigned char)(0x80 | 126);
        hdr[h++] = (unsigned char)(len >> 8);
        hdr[h++] = (unsigned char)(len & 0xFF);
    }
    memcpy(hdr + h, mask, sizeof mask);
    h += sizeof mask;

    if (ws_write_all(c, hdr, h) < 0)
        return -1;
    if (len == 0)
        return 0;

    masked = malloc(len);
    if (!masked)
        return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = payload[i] ^ mask[i & 3];
    rc = ws_write_all(c, masked, len);
    free(masked);
    return rc;
}

/* Handshake HTTP -> WebSocket. */
static int ws_handshake(ws_conn *c, const char *host, const char *path)
{
    unsigned char rnd[16];
    char key[32];
    size_t klen = 0;
    char req[512];
    char resp[1024];
    size_t got = 0;
    int n;

    mbedtls_ctr_drbg_random(&c->drbg, rnd, sizeof rnd);
    if (mbedtls_base64_encode((unsigned char *)key, sizeof key, &klen, rnd, sizeof rnd) != 0)
        return -1;
    key[klen] = '\0';

    n = snprintf(req, sizeof req,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\n"
                 "Sec-WebSocket-Version: 13\r\n"
                 "Origin: https://hack.chat\r\n"
                 "\r\n",
                 path, host, key);
    if (n <= 0 || (size_t)n >= sizeof req)
        return -1;
    if (ws_write_all(c, (const unsigned char *)req, (size_t)n) < 0)
        return -1;

    /* Lit l'en-tête de réponse octet par octet jusqu'à la ligne vide. */
    while (got < sizeof resp - 1) {
        int r = mbedtls_ssl_read(&c->ssl, (unsigned char *)resp + got, 1);
        if (r == 1) {
            got++;
            if (got >= 4 && memcmp(resp + got - 4, "\r\n\r\n", 4) == 0)
                break;
        } else if (r == MBEDTLS_ERR_SSL_WANT_READ) {
            svcSleepThread(1000000LL);
        } else {
            return -1;
        }
    }
    resp[got] = '\0';

    if (strncmp(resp, "HTTP/1.1 101", 12) != 0)
        return -1;
    return 0;
}

int ws_connect(ws_conn **out, const char *host, const char *port,
               const char *path, const char *ca_file)
{
    ws_conn *c;
    int ret = -1;
    int flags;

    *out = NULL;
    c = calloc(1, sizeof *c);
    if (!c)
        return -1;

    mbedtls_net_init(&c->net);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_x509_crt_init(&c->cacert);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);

    ret = mbedtls_entropy_add_source(&c->entropy, ps_entropy, NULL, 32,
                                     MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret) goto fail;

    ret = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy,
                                (const unsigned char *)"hackchat3ds", 11);
    if (ret) goto fail;

    /* Certificats racine : négatif = erreur de lecture du fichier. */
    ret = mbedtls_x509_crt_parse_file(&c->cacert, ca_file);
    if (ret < 0) goto fail;

    ret = mbedtls_net_connect(&c->net, host, port, MBEDTLS_NET_PROTO_TCP);
    if (ret) goto fail;

    ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret) goto fail;

    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c->conf, &c->cacert, NULL);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (ret) goto fail;

    ret = mbedtls_ssl_set_hostname(&c->ssl, host);
    if (ret) goto fail;

    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, NULL);

    do {
        ret = mbedtls_ssl_handshake(&c->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret) goto fail;

    ret = ws_handshake(c, host, path);
    if (ret) goto fail;

    /* Après le handshake : socket non bloquante pour le polling dans la boucle. */
    flags = fcntl(c->net.fd, F_GETFL, 0);
    if (flags < 0 || fcntl(c->net.fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ret = -1;
        goto fail;
    }

    *out = c;
    return 0;

fail:
    conn_free(c);
    free(c);
    return ret ? ret : -1;
}

int ws_send_text(ws_conn *c, const char *text)
{
    size_t n = strlen(text);
    if (n > WS_MAX_SEND)
        return -1;
    return ws_send_frame(c, 0x1, (const unsigned char *)text, n);
}

/* Lecture d'un octet non bloquante : 1 = lu, 0 = rien, -1 = erreur. */
static int ssl_read_some(ws_conn *c, unsigned char *buf, size_t len)
{
    int r = mbedtls_ssl_read(&c->ssl, buf, len);
    if (r > 0)
        return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_TIMEOUT)
        return 0;
    /* Selon la version de newlib, un EAGAIN peut remonter comme erreur réseau. */
    if (r == MBEDTLS_ERR_NET_RECV_FAILED && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 0;
    return -1;
}

/* Lecture bloquante (on attend le reste d'une trame déjà commencée). */
static int ssl_read_full(ws_conn *c, unsigned char *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int r = ssl_read_some(c, buf + got, len - got);
        if (r < 0)
            return -1;
        if (r == 0) {
            svcSleepThread(1000000LL);
            continue;
        }
        got += (size_t)r;
    }
    return 0;
}

int ws_poll_text(ws_conn *c, char *buf, size_t cap)
{
    size_t total = 0;
    int first = 1;

    if (cap == 0)
        return -1;

    for (;;) {
        unsigned char h[2];
        unsigned char ext[8];
        unsigned char *pay;
        size_t plen;
        int fin, opcode, i;

        /* Premier octet : non bloquant (pas de message => retourne 0). */
        if (first) {
            int r = ssl_read_some(c, &h[0], 1);
            if (r < 0) return -1;
            if (r == 0) return 0;
            first = 0;
        } else {
            if (ssl_read_full(c, &h[0], 1) < 0) return -1;
        }
        if (ssl_read_full(c, &h[1], 1) < 0) return -1;

        fin = (h[0] & 0x80) != 0;
        opcode = h[0] & 0x0F;
        plen = h[1] & 0x7F;

        if (plen == 126) {
            if (ssl_read_full(c, ext, 2) < 0) return -1;
            plen = ((size_t)ext[0] << 8) | ext[1];
        } else if (plen == 127) {
            if (ssl_read_full(c, ext, 8) < 0) return -1;
            plen = 0;
            for (i = 0; i < 8; i++)
                plen = (plen << 8) | ext[i];
        }
        if (plen > WS_MAX_FRAME)
            return -1;

        /* Le serveur n'envoie normalement pas de masque ; on le gère quand même. */
        if (h[1] & 0x80) {
            unsigned char m[4];
            if (ssl_read_full(c, m, 4) < 0) return -1;
        }

        if (opcode == 0x8) /* close */
            return -1;

        pay = malloc(plen ? plen : 1);
        if (!pay) return -1;
        if (plen && ssl_read_full(c, pay, plen) < 0) {
            free(pay);
            return -1;
        }

        if (opcode == 0x9) { /* ping -> pong */
            ws_send_frame(c, 0xA, pay, plen);
            free(pay);
            first = (total == 0);
            continue;
        }

        if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0) { /* texte / binaire / suite */
            if (total + plen < cap) {
                if (plen) memcpy(buf + total, pay, plen);
                total += plen;
            }
            free(pay);
            if (fin) {
                buf[total] = '\0';
                return (int)total;
            }
            first = 0; /* message fragmenté : on continue en bloquant */
            continue;
        }

        /* pong ou autre : ignoré */
        free(pay);
        first = (total == 0);
    }
}

void ws_close(ws_conn *c)
{
    if (!c)
        return;
    ws_send_frame(c, 0x8, NULL, 0);
    mbedtls_ssl_close_notify(&c->ssl);
    conn_free(c);
    free(c);
}
