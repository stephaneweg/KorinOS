/* fssvc.h : ce qui est commun aux services de fichiers écrits en C (vfs, fatfs,
 * ramfs, imgblk), communs au Pi et au PC (docs/FICHIERS.md ; protocole :
 * src/inc/fs.inc, src/inc/blk.inc, src/inc/armpi.inc).
 *
 * Les numéros, les codes d'erreur et les décalages sont ceux de l'asm : les
 * protocoles ne changent pas. Le message (512 octets) est vu par struct msg,
 * champ pour champ comme MSG_* (src/kits/common/korin_kit.h). */
#ifndef KORIN_FSSVC_H
#define KORIN_FSSVC_H

#include "korinrt.h"

/* ---- message des services (fs.inc : +0 op / résultat, +8 a ... +48 données) */
struct msg {
	i64 op;
	i64 a, b, c, d;
	u32 len;
	u32 pid;
	u8 data[464];
};
_Static_assert(sizeof(struct msg) == 512, "message de 512 octets");

/* ---- erreurs (armpi.inc, fs.inc) absentes de korinrt.h */
#define E_BUSY -16
#define E_MFILE -24
#define E_NAMETOOLONG -36

/* ---- attributs (armpi.inc) */
#define ATTR_SYS 0x100
#define ATTR_LIB 0x200
#define ATTR_SCRIPT 0x800
#define ATTR_FILE 0x20

/* ---- services (armpi.inc, syscalls.inc) */
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVF_MEMORY 4
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_SIGNAL 0xFFFD
#define SVC_OP_FORK 0xFFFC
#define SVC_OP_EXEC 0xFFFB
#define SVC_PID_KERNEL 0x7FFFFFFF
#define BUF_VA_BASE 0x6800000000ULL /* tampon n : BUF_VA_BASE + n * BUF_SLOT */
#define BUF_SLOT 0x4000000ULL
#define MC_WRITE 1
#define PL_SIZE 64 /* entrée de SYS_PROC_INFO (nom en +24) */
#define BI_SIZE 32
#define VS_FREE_LOW 48 /* SYS_VM_STATS (pages) */
#define VS_FREE_HIGH 56
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4

/* ---- protocole des fichiers (fs.inc) */
#define FS_OPEN 1
#define FS_STAT 2
#define FS_MKDIR 3
#define FS_UNLINK 4
#define FS_UTIME 5
#define FS_RENAME 6
#define FS_READ 10
#define FS_WRITE 11
#define FS_CLOSE 12
#define FS_READDIR 13
#define FS_BUFFER 14
#define FS_READ_BUF 15
#define FS_WRITE_BUF 16
#define FS_READ_DIRECT 17
#define FS_SEEK 26
#define FS_POS_CUR -1
#define FS_DIRECT_MAX 0x100000
#define FS_BUFFER_SIZE 65536
#define VFS_PATHMAP 23
#define VFS_PATHQ 24
#define V_OPEN 30
#define V_MKDIR 32
#define V_UNLINK 33
#define V_CHDIR 34
#define V_GETCWD 35
#define V_EXEC 36
#define V_PIPE 37
#define V_PIPE_READ 38
#define V_PIPE_WRITE 39
#define V_PIPE_CLOSE 40
#define V_FILE_END 44
#define V_END_FILE 45
#define V_PIPE_ROOM 48
#define PIPE_NOWAIT 1
#define PIPE_SHORT 2

/* ---- appels système courts */
#define SC(n) korin_sys0(n)
static inline long sc_print(const char *s) { return korin_sys2(SYS_PRINT, s, strlen(s)); }

/* comme pour l'asm : un service qui ne peut pas démarrer sort avec 1 (init2 relance) */
#define SVC_QUIT() _exit(1)

/* entrée d'un thread (SYS_THREAD_CREATE : entrée, sommet de pile, argument) */
#if defined(__x86_64__)
#define THREAD_ENTRY __attribute__((force_align_arg_pointer, noreturn))
#else
#define THREAD_ENTRY __attribute__((noreturn))
#endif

#endif
