/*
 * WebSocket sur TLS (wss://) pour la 3DS.
 * TLS : mbedTLS. Vérification du serveur avec le fichier CA fourni (romfs).
 */
#ifndef HC_WS_H
#define HC_WS_H

#include <stddef.h>

typedef struct ws_conn ws_conn;

/* Ouvre TLS vers host:port puis fait le handshake WebSocket sur path.
 * ca_file : chemin du fichier PEM des certificats racine (ex: "romfs:/cacert.pem").
 * Retourne 0 si OK, sinon un code négatif ou une erreur mbedTLS (> 0 ou < 0). */
int ws_connect(ws_conn **out, const char *host, const char *port,
               const char *path, const char *ca_file);

/* Envoie un message texte (max 1024 octets). 0 = OK, < 0 = erreur. */
int ws_send_text(ws_conn *c, const char *text);

/* Lecture non bloquante d'un message texte.
 *   > 0 : taille du message copié dans buf (terminé par '\0')
 *   = 0 : rien pour l'instant
 *   < 0 : connexion perdue ou erreur */
int ws_poll_text(ws_conn *c, char *buf, size_t cap);

/* Ferme proprement et libère. */
void ws_close(ws_conn *c);

#endif
