#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"
#include "http.h"

#define MAX_LINES 60000
#define MAX_LINKS 4000

/* ---------- Tampon dynamique ---------- */

typedef struct {
    char *s;
    size_t len;
    size_t cap;
} buf_t;

static int buf_reserve(buf_t *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap)
        return 0;
    {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        char *ns;
        while (nc < b->len + extra + 1)
            nc *= 2;
        ns = realloc(b->s, nc);
        if (!ns)
            return -1;
        b->s = ns;
        b->cap = nc;
    }
    return 0;
}

static void buf_putc(buf_t *b, char c)
{
    if (buf_reserve(b, 1) == 0) {
        b->s[b->len++] = c;
        b->s[b->len] = '\0';
    }
}

static void buf_puts(buf_t *b, const char *s)
{
    size_t l = strlen(s);
    if (buf_reserve(b, l) == 0) {
        memcpy(b->s + b->len, s, l);
        b->len += l;
        b->s[b->len] = '\0';
    }
}

/* Ajoute un saut de ligne, sans en empiler plusieurs. */
static void buf_newline(buf_t *b)
{
    if (b->len > 0 && b->s[b->len - 1] != '\n')
        buf_putc(b, '\n');
}

/* Ajoute un espace, sans doublon ni en début de ligne. */
static void buf_space(buf_t *b)
{
    if (b->len > 0 && b->s[b->len - 1] != ' ' && b->s[b->len - 1] != '\n')
        buf_putc(b, ' ');
}

/* ---------- Caractères : entités et UTF-8 ---------- */

/* Lettres accentuées Latin-1 (U+00C0..U+00FF) vers ASCII lisible sur la console. */
static char latin1_to_ascii(unsigned cp)
{
    static const char table[64] =
        /* C0-C5 */ "AAAAAA"
        /* C6    */ "E"
        /* C7    */ "C"
        /* C8-CB */ "EEEE"
        /* CC-CF */ "IIII"
        /* D0    */ "D"
        /* D1    */ "N"
        /* D2-D6 */ "OOOOO"
        /* D7    */ "x"
        /* D8    */ "O"
        /* D9-DC */ "UUUU"
        /* DD    */ "Y"
        /* DE    */ "T"
        /* DF    */ "s"
        /* E0-E5 */ "aaaaaa"
        /* E6    */ "e"
        /* E7    */ "c"
        /* E8-EB */ "eeee"
        /* EC-EF */ "iiii"
        /* F0    */ "d"
        /* F1    */ "n"
        /* F2-F6 */ "ooooo"
        /* F7    */ "/"
        /* F8    */ "o"
        /* F9-FC */ "uuuu";
    if (cp >= 0xC0 && cp <= 0xFF)
        return table[cp - 0xC0];
    return '?';
}

/* Traduit un code Unicode vers un caractère affichable (ASCII). */
static char cp_to_char(unsigned long cp)
{
    if (cp < 0x80)
        return (char)cp;
    if (cp >= 0xC0 && cp <= 0xFF)
        return latin1_to_ascii((unsigned)cp);
    switch (cp) {
    case 0x2018: case 0x2019: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: return '-';
    case 0x2026: return '.';
    case 0x00A0: return ' ';
    default: return '?';
    }
}

/* Entités nommées courantes. */
static int named_entity(const char *name, unsigned long *cp)
{
    static const struct { const char *n; unsigned long c; } tab[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", ' '}, {"copy", '?'}, {"reg", '?'}, {"hellip", 0x2026},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"eacute", 0xE9}, {"egrave", 0xE8},
        {"agrave", 0xE0}, {"ccedil", 0xE7}, {"ocirc", 0xF4}, {"ecirc", 0xEA},
        {"ugrave", 0xF9}, {"ucirc", 0xFB}, {"icirc", 0xEE}, {"euro", '?'},
        {NULL, 0}
    };
    size_t i;
    for (i = 0; tab[i].n; i++) {
        if (!strcmp(name, tab[i].n)) {
            *cp = tab[i].c;
            return 1;
        }
    }
    return 0;
}

/* Écrit le caractère décodé d'une séquence UTF-8 ; renvoie le nombre d'octets lus. */
static size_t put_utf8(buf_t *b, const char *s, size_t avail)
{
    unsigned char c = (unsigned char)s[0];
    unsigned long cp;
    size_t n;

    if (c < 0x80) {
        buf_putc(b, (char)c);
        return 1;
    }
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else { buf_putc(b, '?'); return 1; }

    if (avail < n) {
        buf_putc(b, '?');
        return 1;
    }
    for (size_t k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[k];
        if ((cc & 0xC0) != 0x80) {
            buf_putc(b, '?');
            return k;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    buf_putc(b, cp_to_char(cp));
    return n;
}

/* Décode une entité à partir de '&' ; renvoie la longueur consommée (0 = pas une entité). */
static size_t decode_entity(buf_t *b, const char *s, size_t avail)
{
    size_t i;
    char name[16];
    unsigned long cp = 0;

    for (i = 1; i < avail && i < 12 && s[i] != ';'; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '#')
            return 0;
    }
    if (i >= avail || s[i] != ';' || i == 1)
        return 0;

    if (s[1] == '#') {
        if (s[2] == 'x' || s[2] == 'X')
            cp = strtoul(s + 3, NULL, 16);
        else
            cp = strtoul(s + 2, NULL, 10);
        buf_putc(b, cp_to_char(cp));
        return i + 1;
    }

    {
        size_t l = i - 1;
        if (l >= sizeof name)
            return 0;
        memcpy(name, s + 1, l);
        name[l] = '\0';
    }
    if (named_entity(name, &cp)) {
        buf_putc(b, cp_to_char(cp));
        return i + 1;
    }
    return 0;
}

/* ---------- Balises ---------- */

static const char *const BLOCK_TAGS[] = {
    "p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "tr", "table", "hr",
    "ul", "ol", "section", "article", "header", "footer", "blockquote", "pre",
    "dt", "dd", "nav", "main", "li", "form", "dl", "option", NULL
};

static int is_block(const char *name)
{
    for (size_t i = 0; BLOCK_TAGS[i]; i++)
        if (!strcmp(name, BLOCK_TAGS[i]))
            return 1;
    return 0;
}

static int is_raw(const char *name)
{
    return !strcmp(name, "script") || !strcmp(name, "style") ||
           !strcmp(name, "head") || !strcmp(name, "noscript") ||
           !strcmp(name, "svg") || !strcmp(name, "template") ||
           !strcmp(name, "iframe") || !strcmp(name, "object");
}

/* Cherche "</name" (insensible à la casse) à partir de s ; renvoie pointeur ou NULL. */
static const char *find_close(const char *s, const char *end, const char *name)
{
    size_t nl = strlen(name);
    for (; s + nl + 2 <= end; s++) {
        if (s[0] == '<' && s[1] == '/') {
            size_t k;
            for (k = 0; k < nl; k++)
                if (tolower((unsigned char)s[2 + k]) != name[k])
                    break;
            if (k == nl)
                return s;
        }
    }
    return NULL;
}

/* Lit l'attribut href (ou un autre) dans le contenu d'une balise. */
static int get_attr(const char *tag, size_t tlen, const char *attr, char *out, size_t cap)
{
    size_t al = strlen(attr);
    const char *p = tag;
    const char *end = tag + tlen;

    while (p < end) {
        size_t k;
        if (!isspace((unsigned char)*p)) {
            p++;
            continue;
        }
        p++;
        for (k = 0; k < al && p + k < end; k++)
            if (tolower((unsigned char)p[k]) != attr[k])
                break;
        if (k == al && p + k < end && p[k] == '=') {
            const char *v = p + k + 1;
            char q = 0;
            size_t l = 0;
            if (v < end && (*v == '"' || *v == '\'')) {
                q = *v;
                v++;
            }
            while (v + l < end && l < cap - 1) {
                if (q ? v[l] == q : (isspace((unsigned char)v[l]) || v[l] == '>'))
                    break;
                out[l] = v[l];
                l++;
            }
            out[l] = '\0';
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/* ---------- Rendu : HTML -> texte brut avec liens ---------- */

typedef struct {
    buf_t text;
    char **links;
    int nlinks;
    int link_open;       /* un <a> est ouvert */
    char pending[HTTP_MAX_URL];
    int pending_ok;      /* l'URL du lien ouvert est exploitable */
    int list_depth;
} render_t;

static void add_link(render_t *r, const char *url)
{
    if (r->nlinks >= MAX_LINKS)
        return;
    r->links = realloc(r->links, sizeof(char *) * (size_t)(r->nlinks + 1));
    if (!r->links)
        return;
    r->links[r->nlinks] = strdup(url);
    r->nlinks++;
}

static void render_tag(render_t *r, const char *inner, size_t ilen, const char *base)
{
    int closing = (ilen > 0 && inner[0] == '/');
    const char *np = closing ? inner + 1 : inner;
    char name[24];
    size_t nl = 0;

    while (np + nl < inner + ilen && nl < sizeof name - 1 &&
           (isalnum((unsigned char)np[nl]) || np[nl] == '-' || np[nl] == '_')) {
        name[nl] = (char)tolower((unsigned char)np[nl]);
        nl++;
    }
    name[nl] = '\0';

    if (!strcmp(name, "a")) {
        if (closing) {
            if (r->link_open && r->pending_ok) {
                char marker[24];
                snprintf(marker, sizeof marker, "[%d]", r->nlinks + 1);
                buf_puts(&r->text, marker);
                add_link(r, r->pending);
            }
            r->link_open = 0;
        } else {
            char href[HTTP_MAX_URL];
            r->link_open = 1;
            r->pending_ok = 0;
            if (get_attr(inner, ilen, "href", href, sizeof href) &&
                http_resolve(base, href, r->pending, sizeof r->pending) == 0)
                r->pending_ok = 1;
        }
        return;
    }

    if (!strcmp(name, "img")) {
        char alt[128];
        if (!closing && get_attr(inner, ilen, "alt", alt, sizeof alt) && alt[0]) {
            buf_space(&r->text);
            buf_putc(&r->text, '[');
            buf_puts(&r->text, alt);
            buf_putc(&r->text, ']');
            buf_space(&r->text);
        }
        return;
    }

    if (!strcmp(name, "ul") || !strcmp(name, "ol")) {
        if (closing) {
            if (r->list_depth > 0)
                r->list_depth--;
        } else {
            r->list_depth++;
        }
        buf_newline(&r->text);
        return;
    }

    if (!strcmp(name, "li") && !closing) {
        buf_newline(&r->text);
        for (int d = 1; d < r->list_depth; d++)
            buf_puts(&r->text, "  ");
        buf_puts(&r->text, "- ");
        return;
    }

    if (is_block(name)) {
        buf_newline(&r->text);
        if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !closing)
            buf_newline(&r->text);
        if (!strcmp(name, "hr"))
            buf_puts(&r->text, "----------------------------------------");
        if (!strcmp(name, "br"))
            return;
        buf_newline(&r->text);
    }
}

static int render_html(render_t *r, const char *s, size_t len, const char *base)
{
    size_t i = 0;

    while (i < len) {
        char c = s[i];

        if (c == '<') {
            /* commentaire */
            if (i + 4 <= len && !strncmp(s + i, "<!--", 4)) {
                const char *e = NULL;
                for (size_t k = i + 4; k + 3 <= len; k++)
                    if (!strncmp(s + k, "-->", 3)) { e = s + k + 3; break; }
                i = e ? (size_t)(e - s) : len;
                continue;
            }
            /* <!DOCTYPE ...> et <? ?> */
            if (i + 1 < len && (s[i + 1] == '!' || s[i + 1] == '?')) {
                while (i < len && s[i] != '>')
                    i++;
                i++;
                continue;
            }
            /* balise */
            {
                size_t j = i + 1;
                char quote = 0;
                while (j < len) {
                    if (quote) {
                        if (s[j] == quote) quote = 0;
                    } else if (s[j] == '"' || s[j] == '\'') {
                        quote = s[j];
                    } else if (s[j] == '>') {
                        break;
                    }
                    j++;
                }
                if (j >= len) {
                    /* '<' non fermé : on le traite comme du texte */
                    buf_putc(&r->text, '<');
                    i++;
                    continue;
                }
                {
                    const char *inner = s + i + 1;
                    size_t ilen = j - (i + 1);
                    char name[24];
                    size_t nl = 0;
                    const char *np = (ilen > 0 && inner[0] == '/') ? inner + 1 : inner;
                    size_t npl = ilen - (size_t)(np - inner);
                    int closing = (ilen > 0 && inner[0] == '/');

                    while (nl < npl && nl < sizeof name - 1 &&
                           (isalnum((unsigned char)np[nl]) || np[nl] == '-')) {
                        name[nl] = (char)tolower((unsigned char)np[nl]);
                        nl++;
                    }
                    name[nl] = '\0';

                    if (!closing && is_raw(name)) {
                        /* saute tout le contenu jusqu'à la balise fermante */
                        const char *close = find_close(s + j + 1, s + len, name);
                        if (close) {
                            const char *gt = memchr(close, '>', (size_t)((s + len) - close));
                            i = gt ? (size_t)(gt - s) + 1 : len;
                        } else {
                            i = len;
                        }
                        if (!strcmp(name, "head"))
                            buf_newline(&r->text);
                        continue;
                    }

                    render_tag(r, inner, ilen, base);
                }
                i = j + 1;
                continue;
            }
        }

        if (c == '&') {
            size_t used = decode_entity(&r->text, s + i, len - i);
            if (used) {
                i += used;
                continue;
            }
            buf_putc(&r->text, '&');
            i++;
            continue;
        }

        if ((unsigned char)c >= 0x80) {
            i += put_utf8(&r->text, s + i, len - i);
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            buf_space(&r->text);
            i++;
            continue;
        }

        buf_putc(&r->text, c);
        i++;
    }
    return 0;
}

/* ---------- Coupure en lignes ---------- */

static int push_line(html_page *p, const char *s, size_t len)
{
    char **nl;
    char *copy;

    if (p->nlines >= MAX_LINES)
        return -1;
    nl = realloc(p->lines, sizeof(char *) * (size_t)(p->nlines + 1));
    if (!nl)
        return -1;
    p->lines = nl;
    copy = malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, s, len);
    copy[len] = '\0';
    p->lines[p->nlines++] = copy;
    return 0;
}

/* Coupe un paragraphe (sans '\n') en lignes de HTML_WRAP_WIDTH au maximum. */
static void wrap_paragraph(html_page *p, const char *para)
{
    char line[HTML_WRAP_WIDTH + 1];
    size_t ll = 0;
    const char *w = para;

    if (!*para) {
        push_line(p, "", 0);
        return;
    }

    while (*w) {
        const char *we = strchr(w, ' ');
        size_t wl = we ? (size_t)(we - w) : strlen(w);

        if (wl == 0) {
            w++;
            continue;
        }

        /* mot plus long que la largeur : on le découpe */
        while (wl > HTML_WRAP_WIDTH) {
            if (ll > 0) {
                push_line(p, line, ll);
                ll = 0;
            }
            push_line(p, w, HTML_WRAP_WIDTH);
            w += HTML_WRAP_WIDTH;
            wl -= HTML_WRAP_WIDTH;
        }

        if (ll == 0) {
            memcpy(line, w, wl);
            ll = wl;
        } else if (ll + 1 + wl <= HTML_WRAP_WIDTH) {
            line[ll++] = ' ';
            memcpy(line + ll, w, wl);
            ll += wl;
        } else {
            push_line(p, line, ll);
            memcpy(line, w, wl);
            ll = wl;
        }

        w += wl;
        if (*w == ' ')
            w++;
    }
    if (ll > 0)
        push_line(p, line, ll);
}

static void wrap_text(html_page *p, const char *text)
{
    const char *start = text;
    int blank_run = 0;

    for (;;) {
        const char *nl = strchr(start, '\n');
        size_t plen = nl ? (size_t)(nl - start) : strlen(start);
        char *para = malloc(plen + 1);

        if (!para)
            return;
        memcpy(para, start, plen);
        para[plen] = '\0';

        if (plen == 0) {
            /* au plus deux lignes vides de suite */
            blank_run++;
            if (blank_run <= 2)
                push_line(p, "", 0);
        } else {
            blank_run = 0;
            wrap_paragraph(p, para);
        }
        free(para);

        if (!nl)
            break;
        start = nl + 1;
    }
}

/* ---------- API ---------- */

int html_render(const char *html, size_t hlen, const char *base, html_page *out)
{
    render_t r;
    memset(&r, 0, sizeof r);
    memset(out, 0, sizeof *out);

    render_html(&r, html, hlen, base);
    buf_newline(&r.text);

    if (r.text.s)
        wrap_text(out, r.text.s);
    else
        push_line(out, "", 0);

    out->links = r.links;
    out->nlinks = r.nlinks;
    free(r.text.s);
    return 0;
}

void html_plain(const char *text, html_page *out)
{
    memset(out, 0, sizeof *out);
    wrap_text(out, text);
}

void html_page_free(html_page *p)
{
    for (int i = 0; i < p->nlines; i++)
        free(p->lines[i]);
    free(p->lines);
    for (int i = 0; i < p->nlinks; i++)
        free(p->links[i]);
    free(p->links);
    memset(p, 0, sizeof *p);
}
