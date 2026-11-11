/* Codes d'erreur des appels système : mêmes valeurs que les E_* des .inc
 * (src/inc/fs.inc, armpi.inc, noyau du Pi). */
#ifndef KORIN_ERRNO_H
#define KORIN_ERRNO_H

#define E_PERM -1
#define E_NOENT -2
#define E_SRCH -3
#define E_INTR -4
#define E_IO -5
#define E_NOEXEC -8
#define E_BADF -9
#define E_AGAIN -11
#define E_NOMEM -12
#define E_FAULT -14
#define E_BUSY -16
#define E_EXIST -17
#define E_NODEV -19
#define E_INVAL -22
#define E_NOSPC -28
#define E_PIPE -32
#define E_DEADLK -35
#define E_NOSYS -38
#define E_TIMEDOUT -110

#endif
