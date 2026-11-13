/* sysfront.h - un programme à deux visages (acpidump, i2chid), comme iwl : le même binaire est dans
 * SD0:/bin (sans droits) et dans SD0:/sys/svc (avec les droits de SD0:/sys). Lancé de SD0:/bin, il
 * relance SD0:/sys/svc/<nom> avec les mêmes arguments (par init2 : le fils n'a ni Terminal ni tube) et
 * recopie au Terminal les lignes « <nom> : … » que le fils écrit au journal du noyau, jusqu'à sa fin.
 * Le fils écrit par slog : au journal (SYS_PRINT), et à la sortie standard au bureau ou dans un tube. */
#ifndef KORIN_SYSFRONT_H
#define KORIN_SYSFRONT_H

#include "korinrt.h"
#include <stdarg.h>

/* le préfixe des lignes (« acpidump »), avant tout slog */
void slog_name(const char *name);
void slog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vslog(const char *fmt, va_list ap);
/* 1 : ce process a les droits de SD0:/sys */
int sysfront_rights(void);
/* sans droits : lance path avec argv[1..] et recopie ses lignes jusqu'à sa fin, ou jusqu'à une ligne
 * qui contient until (0 : jusqu'à la fin ; un service qui reste en marche) -> 0 si lancé */
int sysfront_run(const char *path, int argc, char **argv, const char *until);
/* suivre le journal du noyau, même quand son anneau de 64 Ko a tourné : klog_follow_start note la fin,
 * puis chaque klog_follow passe à fn les lignes complètes écrites depuis (sans horodatage, '\n' compris) */
void klog_follow_start(void);
void klog_follow(void (*fn)(const char *line, long len));

#endif
