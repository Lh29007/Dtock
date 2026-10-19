/*
 * Protocole hack.chat (JSON) et état du client : salon courant, utilisateurs,
 * journal des messages, liste des salons publics.
 * Noms de commandes et d'événements repris de hackchat-engine.
 */
#ifndef HC_H
#define HC_H

#include <stddef.h>
#include <stdint.h>

#define HC_MAX_USERS     128
#define HC_MAX_LOG       200
#define HC_MAX_CHANNELS  64
#define HC_TEXT_MAX      480

typedef enum {
    HC_MSG_CHAT = 0,
    HC_MSG_INFO,
    HC_MSG_JOIN,
    HC_MSG_LEAVE,
    HC_MSG_EMOTE
} hc_msg_kind;

typedef struct {
    int used;
    int userid;
    int me;
    char nick[32];
    uint32_t color;     /* couleur RGBA (format citro2d) */
} hc_user;

typedef struct {
    hc_msg_kind kind;
    char nick[32];
    uint32_t color;
    char text[HC_TEXT_MAX];
} hc_msg;

typedef struct {
    char nick[32];                    /* mon pseudo */
    char channel[64];                 /* salon courant ; "" si aucun */
    int my_id;                        /* userid du client (-1 si inconnu) */
    hc_user users[HC_MAX_USERS];      /* utilisateurs du salon courant */
    hc_msg log[HC_MAX_LOG];
    int nlog;
    char channels[HC_MAX_CHANNELS][64];
    int nchannels;
    int selected_channel;             /* index dans channels[] */
    int connected;
} hc_state;

void hc_init(hc_state *st, const char *nick);

/* Commandes client -> serveur (JSON). Retournent la longueur écrite. */
int hc_build_join(char *out, size_t cap, const char *nick, const char *channel);
int hc_build_chat(char *out, size_t cap, const char *channel, const char *text);
int hc_build_leave(char *out, size_t cap, const char *channel);
int hc_build_getchannels(char *out, size_t cap);

/* Traite un message serveur et met à jour l'état. */
void hc_handle(hc_state *st, const char *json);

/* Ajoute une ligne d'information dans le journal (ex: "Connexion..."). */
void hc_log_info(hc_state *st, const char *text);

/* Vide la liste des utilisateurs (changement de salon). */
void hc_clear_users(hc_state *st);

#endif
