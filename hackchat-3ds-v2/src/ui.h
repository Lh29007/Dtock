/*
 * Interface graphique (citro2d) : écran du haut = journal + panneau utilisateurs,
 * écran du bas = salons, boutons tactiles, zone de saisie.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "hc.h"

#define UI_LIST_ROWS 5   /* salons visibles dans la liste */

typedef enum {
    UI_HIT_NONE = 0,
    UI_HIT_CHANNEL,     /* index dans la liste des salons */
    UI_HIT_REFRESH,     /* actualiser la liste des salons */
    UI_HIT_LEAVE,       /* quitter le salon courant */
    UI_HIT_RECONNECT,   /* reconnecter (si déconnecté) */
    UI_HIT_INPUT        /* écrire un message */
} ui_hit_kind;

typedef struct {
    ui_hit_kind kind;
    int index;
} ui_hit;

typedef struct {
    int panel_open;     /* panneau utilisateurs à droite (écran du haut) */
    int log_scroll;     /* lignes remontées depuis le bas du journal */
    int list_offset;    /* premier salon visible dans la liste */
} ui_view;

void ui_init(void);
void ui_exit(void);

/* Dessine les deux écrans pour une frame. */
void ui_draw(const hc_state *st, const ui_view *v);

/* Quelle zone tactile (écran du bas) a été touchée. */
ui_hit ui_hit_test(const hc_state *st, const ui_view *v, int tx, int ty);

#endif
