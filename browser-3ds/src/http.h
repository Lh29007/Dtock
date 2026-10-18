/*
 * Requêtes HTTPS GET pour la 3DS (mbedTLS).
 * Suit les redirections, décode le chunked, n'accepte que HTTPS
 * (les URL http:// sont passées en https://).
 */
#ifndef BROWSER_HTTP_H
#define BROWSER_HTTP_H

#include <stddef.h>

#define HTTP_MAX_URL 1024

typedef struct {
    unsigned char *body;   /* corps de la réponse (malloc), terminé par '\0' */
    size_t len;
    int status;            /* code HTTP de la dernière réponse */
    char final_url[HTTP_MAX_URL]; /* URL après redirections */
} http_result;

/* GET de url. ca_file : fichier PEM des racines (ex. "romfs:/cacert.pem").
 * Retourne 0 si une réponse a été reçue (même 404), -1 sinon (voir http_error). */
int http_get(const char *url, const char *ca_file, http_result *out);
void http_result_free(http_result *r);

/* Résout href par rapport à base. Retourne 0 si l'URL est exploitable en HTTPS,
 * -1 sinon (javascript:, mailto:, ancre seule, etc.). */
int http_resolve(const char *base, const char *href, char *out, size_t cap);

const char *http_error(void);

#endif
