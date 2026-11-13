/* IPC entre process (core/ipc.c) : poignées, canaux locaux, mémoire partagée,
 * comme src/kernel/ipc.asm du Pi (docs/ABI.md, 0x70-0x7D). */
#ifndef KORIN_IPC_H
#define KORIN_IPC_H

#include <stdint.h>

struct process;
struct ipc_obj;

int64_t sys_handle_close(uint64_t h);
int64_t sys_handle_info(uint64_t h, uint64_t out);
int64_t sys_handle_wait(uint64_t entries, uint64_t n, int64_t ms);
int64_t sys_handle_list(uint64_t out, uint64_t max, uint64_t clear);
int64_t sys_handle_inherit(uint64_t entries, uint64_t n);
int64_t sys_chan_create(uint64_t type, uint64_t out);
int64_t sys_chan_send(uint64_t h, uint64_t msg, uint64_t flags, uint64_t ms);
int64_t sys_chan_recv(uint64_t h, uint64_t msg, uint64_t flags, uint64_t ms);
int64_t sys_chan_shutdown(uint64_t h, uint64_t how);
int64_t sys_shm_create(uint64_t size, uint64_t flags);
int64_t sys_shm_open(uint64_t name, uint64_t flags);
int64_t sys_shm_unlink(uint64_t name);
int64_t sys_shm_ctl(uint64_t h, uint64_t op, uint64_t val);
int64_t sys_shm_map(uint64_t h, uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags, uint64_t off);

/* fin d'un process : poignées fermées, liste de lancement rendue */
void ipc_proc_exit(struct process *p);
/* fork : le fils tient les mêmes poignées (mêmes numéros) */
int ipc_fork(struct process *parent, struct process *child);
/* lancement : la liste HANDLE_INHERIT du père passe au fils */
void ipc_inherit(struct process *parent, struct process *child);

/* exec : la liste EXP_MARKS {poignée, marque} (n paires) ; _ok : toutes ouvertes ? */
int ipc_exec_marks_ok(const uint32_t *e, int n);
void ipc_exec_marks(const uint32_t *e, int n);

/* objets partagés projetés (core/mm.c) */
void ipc_obj_get(struct ipc_obj *o);
void ipc_obj_put(struct ipc_obj *o);
/* page de l'objet à ce décalage (créée à zéro) ; 0 au-delà de la taille */
uint64_t ipc_shm_page(struct ipc_obj *o, uint64_t off);
void ipc_shm_mapped(struct ipc_obj *o, int delta);

#endif
