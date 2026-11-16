/* Services nommés (core/svc.c) : l'IPC du micronoyau, comme sur le Pi
 * (docs/ABI.md, 0x30). Message de 512 octets : +0 op / résultat, +8 a,
 * +16 b, +24 c, +32 d, +40 longueur, +44 pid de l'appelant (rempli par le
 * noyau), +48 données. */
#ifndef KORIN_SVC_H
#define KORIN_SVC_H

#include <stdint.h>

struct process;

#define SVC_MSG 512
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVF_MEMORY 4
#define SVF_POST 8
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_SIGNAL 0xFFFD
#define SVC_OP_EXEC 0xFFFB
#define SVC_OP_SETUID 0xFFFA
#define SPAWN_UID 2

void svc_notify_setuid(int pid, int uid);
int64_t sys_svc_register(uint64_t name, uint64_t flags);
int64_t sys_svc_open(uint64_t name, uint64_t ms);
int64_t sys_svc_call(uint64_t chan, uint64_t msg);
int64_t sys_svc_next(uint64_t chan, uint64_t msg, uint64_t ms);
int64_t sys_svc_reply(uint64_t num, uint64_t msg);
int64_t sys_svc_signal(uint64_t chan, uint64_t bits);
int64_t sys_svc_forward(uint64_t num, uint64_t chan, uint64_t msg);
int64_t sys_svc_map_caller(uint64_t num, uint64_t addr, uint64_t size, uint64_t flags);
int64_t sys_svc_unmap_caller(uint64_t addr);
int64_t sys_svc_caller_phys(uint64_t addr, uint64_t out, uint64_t places);
/* mémoire basse : SVC_SIG_LOWMEM aux services SVF_MEMORY */
void svc_low_memory(void);
/* enregistrement par le noyau (WS_REGISTER) ; un service de p a-t-il ce drapeau ? */
int64_t svc_register_name(const char *name, uint32_t flags, struct process *p);
int svc_owner_has(struct process *p, uint32_t flag);

/* depuis le noyau (interruptions des pilotes) : OU dans les signaux */
void svc_signal_kernel(int chan, uint64_t bits);
void svc_notify(uint32_t flag, uint64_t op, int64_t a, int64_t b);
/* avis de naissance (SVF_BIRTHS) : a fils, b père, c entrée, d sortie, données (8 o) erreur
 * standard du fils (poignées de tubes de vfs ou de bouts de vterm ; 0 : celle du père, -1 :
 * aucune) ; stdio = 0 : toutes héritées */
void svc_notify_birth(int child, int parent, const int64_t *stdio);
/* exec avec des entrées / sorties nouvelles : avis SVC_OP_EXEC à vfs (a = b = pid, c entrée, d sortie,
 * données (8 o) erreur ; 0 : inchangée, -1 : aucune), exec_stdio du Pi */
void svc_notify_exec(int pid, const int64_t *stdio);
void svc_notify_fin(int pid, int code);
void svc_proc_exit(struct process *p);

#endif
