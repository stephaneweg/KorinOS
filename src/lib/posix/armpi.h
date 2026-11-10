/*
 * armpi.h -- le cote ARMPI de libposix (docs/POSIX.md) : appels systeme, codes d'erreur, kits,
 * services. Prive a la bibliotheque (pas installe).
 *
 *  - Appels systeme : svc 0, numero dans x8 (SYS_* d'armpi_sys.h, genere par
 *    tools/posix/build.sh depuis src/inc/syscalls.inc : la seule source des numeros), arguments
 *    x0..x5, resultat dans x0 (negatif : une erreur d'ARMPI, numeros de Linux) ;
 *  - fichiers, repertoires, chemins, entree / sortie standard : le client de fichiers d'AppKit,
 *    fichier_sys (SYS_OPEN..SYS_SPAWN_PIPE, SYS_PRINT : memes numeros et resultats qu'un
 *    svc 0), et les fonctions AAPCS de la table d'AppKit (f_spawn_file, f_kit_load) ;
 *  - tubes et images des programmes : le service vfs directement (V_PIPE*, V_EXEC : src/inc/fs.inc) ;
 *  - le reseau : NetKit (net_* : tous les registres gardes sauf x0).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _POSIX_ARMPI_H
#define _POSIX_ARMPI_H

#include "armpi_sys.h"

/* ---- appels systeme ---------------------------------------------------------------------- */
static inline long __armpi_sys (long n, long a, long b, long c, long d, long e, long f)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d;
	register long x4 __asm__ ("x4") = e;
	register long x5 __asm__ ("x5") = f;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5), "r" (x8)
			  : "memory");
	return x0;
}
#define __armpi_sys0(n)			__armpi_sys ((n), 0, 0, 0, 0, 0, 0)
#define __armpi_sys1(n, a)		__armpi_sys ((n), (long) (a), 0, 0, 0, 0, 0)
#define __armpi_sys2(n, a, b)		__armpi_sys ((n), (long) (a), (long) (b), 0, 0, 0, 0)
#define __armpi_sys3(n, a, b, c)	__armpi_sys ((n), (long) (a), (long) (b), (long) (c), 0, 0, 0)
#define __armpi_sys6(n, a, b, c, d, e, f) \
	__armpi_sys ((n), (long) (a), (long) (b), (long) (c), (long) (d), (long) (e), (long) (f))

/* ---- erreurs ------------------------------------------------------------------------------ */
/* code d'erreur d'ARMPI (negatif, numeros de Linux) -> errno de newlib */
int __armpi_errno (long r);
/* r >= 0 : r ; r < 0 : errno pose, -1 */
long __armpi_ret (long r);

/* ---- AppKit : le client de fichiers (fichier_sys : x8 = numero, x0..x2 arguments) ---------------- */
long __armpi_fsys (long n, long a, long b, long c);
#define ARMPI_O_READ		1		/* drapeaux d'ouverture d'AppKit (src/inc/armpi.inc) */
#define ARMPI_O_WRITE		2
#define ARMPI_O_CREATE		4
#define ARMPI_O_TRUNC		8
#define ARMPI_O_APPEND		16
#define ARMPI_O_NONBLOCK	32
#define ARMPI_ATTR_DIR		0x10
#define ARMPI_DIRENT_SIZE	256		/* {name[256], size (+256), attributes (+264)} */
#define ARMPI_DIRENT_ATTR	264
#define ARMPI_DIRENT_BYTES	272
#define ARMPI_PATH_MAX		255		/* plus long chemin d'AppKit */

/* les kits : AppKit (ouvert au demarrage), NetKit (au premier socket) */
struct __armpi_kit
{
	unsigned magic, count;			/* 'AKIT', nombre de fonctions (la version) */
	unsigned off[];				/* fonction i a (char *) kit + off[i] */
};
const struct __armpi_kit *__armpi_kit_open (const char *name, unsigned min);
void *__armpi_appkit (unsigned rank);		/* une fonction d'AppKit (0 : aucune) */
void *__armpi_netkit (unsigned rank);		/* une fonction de NetKit (0 : pas de NetKit) */
#define AK_F_SPAWN_FILE		29		/* (reponse de V_EXEC, arguments, poignees[3]) -> pid */
#define AK_F_KIT_LOAD		31		/* (name) -> 0 */
#define AK_F_FORK		46		/* () -> pid (pere), 0 (fils) : positions partagees, fils remis en etat */
#define AK_F_LIRE_A		48		/* (fd, tampon, n, position) -> n : pread */
#define AK_F_ECRIRE_A		49		/* (fd, tampon, n, position) -> n : pwrite */
#define AK_F_EXEC_FILE		51		/* (reponse de V_EXEC, params EXP_* 72 o) -> erreur (sinon ne revient pas) */
#define AK_F_EXEC_REPRISE	52		/* () : l'etat d'AppKit d'avant un exec, repris (au demarrage) */
#define ARMPI_ATTR_SYS		0x100		/* (V_EXEC) fichier sous SD0:/sys/ */
#define ARMPI_ATTR_EXEC		0x400		/* (V_STATX) fichier executable (regles de chemin, index) */
#define ARMPI_ATTR_SCRIPT	0x800		/* (V_EXEC) un script : son interprete est ouvert */
#define ARMPI_EXEC_FC_TAG	0x40000000u	/* marque de l'etat d'AppKit passe par exec (syscalls.inc) */
#define AK_MIN_VERSION		32
#define NK_NET_SOCKET		3		/* (type 1 TCP / 2 UDP) -> sd */
#define NK_NET_CONNECT		4		/* (sd, ip, port) */
#define NK_NET_BIND		5		/* (sd, port) */
#define NK_NET_LISTEN		6		/* (sd, backlog) */
#define NK_NET_ACCEPT		7		/* (sd, ms) -> sd, E_AGAIN */
#define NK_NET_SEND		8		/* (sd, buf, n) -> n */
#define NK_NET_RECV		9		/* (sd, tampon, max, ms) -> n (0 : fermee), E_AGAIN */
#define NK_NET_CLOSE		10
#define NK_NET_SENDTO		11		/* (sd, buf, n, ip, port) */
#define NK_NET_RECVFROM		12		/* (sd, buf, max, ms, &{ip, port}) */
#define NK_NET_RESOLVE		13		/* (nom) -> IPv4 (ordre de l'hote), < 0 */
#define NK_NET_INFO		14		/* (netinfo 32 o) */
#define NK_NET_SHUTDOWN		29		/* (sd) : FIN apres la file, on recoit toujours */
#define NK_NET_TIME		17		/* (&{utc, ntp, ms}) -> secondes UTC (0 : inconnue) */
#define NK_NET_FORK_CHILD	32		/* () : dans le fils d'un fork, le tampon du pere oublie */
#define NK_MIN_VERSION		18
#define ARMPI_NET_FOREVER	(~0UL)		/* attente de NetKit : sans limite */

/* ---- le service vfs (src/inc/fs.inc) ------------------------------------------------------ */
struct __armpi_msg				/* message a un service (512 octets) */
{
	long op, a, b, c, d;
	unsigned len, pid;
	unsigned char data[464];
};
long __armpi_vfs_call (struct __armpi_msg *m);	/* -> resultat (reponse dans m) */
#define V_STAT			31		/* chemin -> b taille, c attributs, d date (secondes UTC) */
#define V_EXEC			36
#define V_PIPE			37
#define V_PIPE_READ		38
#define V_PIPE_WRITE		39
#define V_PIPE_CLOSE		40
#define V_STDIO			41
#define V_FILE_END		44		/* a mode (1 lecture, 2 ecriture), chemin -> a poignee */
#define V_UTIME			42		/* b secondes UTC, chemin */
#define V_RENAME		43		/* ancien, 0, nouveau, 0 ; E_XDEV : volumes differents */
#define V_STATX			46		/* comme V_STAT ; c + ARMPI_ATTR_EXEC : executable */
#define V_CHMOD			47		/* a = mode, chemin : index des executables de vfs */
#define VFS_PATHQ		24		/* donnees : chemin -> 1 s'il est virtuel (SD0:/etc/paths.txt) */
#define PIPE_NOWAIT		1
#define PIPE_COURT		2		/* V_PIPE_READ : 1 s d'attente au plus */
#define ARMPI_PIPE_CHUNK	464

/* ---- le process --------------------------------------------------------------------------- */
#define ARMPI_USER_STACK_TOP	0x7FFFFF0000UL	/* pile du thread principal : 64 Mo dessous, a la demande */
#define ARMPI_USER_STACK_SIZE	(64UL << 20)
#define ARMPI_PROCLIST_ENT	64		/* SYS_PROCLIST: pid, threads, heap KB, name[32] (+24), state (+56) */

#endif /* _POSIX_ARMPI_H */
