/*
 * Protocole hack.chat (JSON) : construction des commandes et affichage des événements.
 * Basé sur hackchat-engine (cmd: join / chat ; événements: session, chat, onlineSet, ...).
 */
#ifndef HC_H
#define HC_H

#include <stddef.h>

#define HC_MAX_USERS 256

typedef struct {
    int used;
    int userid;
    char nick[32];
} hc_user;

typedef struct {
    hc_user users[HC_MAX_USERS];
    char channel[64];
} hc_state;

/* Commandes client -> serveur (JSON), retournent la longueur écrite. */
int hc_build_join(char *out, size_t cap, const char *nick, const char *channel);
int hc_build_chat(char *out, size_t cap, const char *channel, const char *text);

/* Traite un message serveur et l'affiche sur la console. */
void hc_handle(hc_state *st, const char *json);

#endif
