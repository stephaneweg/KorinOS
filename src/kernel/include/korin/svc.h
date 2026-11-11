/* Services nommés (core/svc.c) : l'IPC du micronoyau, comme sur le Pi
 * (docs/ABI.md, 0x30). Message de 512 octets : +0 op / résultat, +8 a,
 * +16 b, +24 c, +32 d, +40 longueur, +44 pid de l'appelant (rempli par le
 * noyau), +48 données. */
#ifndef KORIN_SVC_H
#define KORIN_SVC_H

#include <stdint.h>

struct process;

#define SVC_MSG 512
#define SVF_FINS 1
#define SVF_NAISSANCES 2
#define SVF_MEMORY 4
#define SVF_POST 8
#define SVC_OP_FIN 0xFFFF
#define SVC_OP_NAISSANCE 0xFFFE
#define SVC_OP_SIGNAL 0xFFFD

int64_t sys_svc_register(uint64_t name, uint64_t flags);
int64_t sys_svc_open(uint64_t name, uint64_t ms);
int64_t sys_svc_call(uint64_t chan, uint64_t msg);
int64_t sys_svc_next(uint64_t chan, uint64_t msg, uint64_t ms);
int64_t sys_svc_reply(uint64_t num, uint64_t msg);
int64_t sys_svc_signal(uint64_t chan, uint64_t bits);
int64_t sys_svc_forward(uint64_t num, uint64_t chan, uint64_t msg);

/* depuis le noyau (interruptions des pilotes) : OU dans les signaux */
void svc_signal_kernel(int chan, uint64_t bits);
void svc_notify_birth(int child, int parent);
void svc_notify_fin(int pid, int code);
void svc_proc_exit(struct process *p);

#endif
