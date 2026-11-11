/* Process, threads et ordonnanceur (préemptif, tourniquet, un cœur pour
 * l'instant). Portable : l'architecture fournit la pile et la bascule. */
#ifndef KORIN_SCHED_H
#define KORIN_SCHED_H

#include <stdint.h>

struct vm_space;

enum thread_state { T_READY, T_RUNNING, T_SLEEPING, T_DEAD };
enum proc_state { P_ALIVE, P_ZOMBIE };

struct process {
	int pid;
	char name[32];
	struct vm_space *vm; /* 0 : process du noyau */
	int threads;
	enum proc_state state;
	int exit_code;
	struct process *next;
};

struct thread {
	uint64_t arch_sp;    /* pile du noyau sauvée par la bascule */
	uint64_t kstack;     /* base de la pile du noyau */
	uint64_t kstack_top;
	int tid;
	enum thread_state state;
	uint64_t wake_ms;
	struct process *proc;
	struct thread *next; /* tous les threads */
	void (*start)(void *);
	void *arg;
	uint64_t user_entry, user_stack;
	char name[24];
};

#define KSTACK_SIZE 16384

extern struct process kernel_process;

void sched_init(void);
struct thread *sched_current(void);
struct thread *thread_create(struct process *p, const char *name, void (*fn)(void *), void *arg);
void schedule(void);
void sched_yield(void);
void sched_sleep(uint64_t ms);
__attribute__((noreturn)) void thread_exit(int code);
/* tout le process courant s'arrête (exit, faute) */
__attribute__((noreturn)) void proc_exit(int code);
void sched_tick(void);      /* minuterie, interruptions coupées */
void sched_irq_exit(void);  /* fin d'interruption : bascule si le quantum est écoulé */
void sched_reap(void);      /* thread inactif : libère threads et process finis */

/* un process depuis une image ELF ; pid ou erreur négative */
int proc_spawn(const char *name, const void *image, uint64_t size);
void proc_list(void);

#endif
