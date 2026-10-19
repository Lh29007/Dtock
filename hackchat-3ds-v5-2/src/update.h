/*
 * Mise à jour : télécharge la dernière release hackchat-v5 (hackchat3dsV5.3dsx)
 * depuis GitHub dans sdmc:/3ds/hackchat/.
 * Partagé entre la V5 (bouton « Mettre à jour ») et hackchat3dsPre-V4 (au lancement).
 */
#ifndef HC_UPDATE_H
#define HC_UPDATE_H

#define UPDATE_API_URL  "https://api.github.com/repos/Lh29007/Dtock/releases?per_page=20"
#define UPDATE_TAG      "hackchat-v5.2-build-"
#define UPDATE_CA       "romfs:/cacert.pem"
#define UPDATE_DIR      "sdmc:/3ds/hackchat"
#define UPDATE_FILE     "sdmc:/3ds/hackchat/hackchat3dsV5.2.3dsx"

/* Reçoit chaque ligne de progression (ctx = paramètre donné à update_run). */
typedef void (*update_log_fn)(const char *line, void *ctx);

/* Retourne 0 si UPDATE_FILE a été mis à jour, -1 sinon (la raison est loggée). */
int update_run(update_log_fn log, void *ctx);

#endif
