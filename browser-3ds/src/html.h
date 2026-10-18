/*
 * HTML -> texte pour l'écran de la 3DS.
 * Pas de JavaScript, pas de CSS : on garde le texte, les titres, les listes,
 * et les liens (numérotés, URL absolues).
 */
#ifndef BROWSER_HTML_H
#define BROWSER_HTML_H

#include <stddef.h>

typedef struct {
    char **lines;     /* lignes déjà coupées à la largeur d'écran (malloc) */
    int nlines;
    char **links;     /* URL absolues, indexées à partir de 0 (malloc) */
    int nlinks;
} html_page;

/* Rend html (longueur hlen) ; base = URL de la page (pour résoudre les liens). */
int html_render(const char *html, size_t hlen, const char *base, html_page *out);
void html_page_free(html_page *p);

/* Texte d'une page d'erreur ou simple (pas de HTML). */
void html_plain(const char *text, html_page *out);

#define HTML_WRAP_WIDTH 48

#endif
