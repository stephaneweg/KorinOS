/* Process, threads et ordonnanceur : tourniquet préemptif sur tous les cœurs
 * (une file commune, un thread inactif par cœur). Portable : l'architecture
 * fournit la pile, l'état FPU et la bascule. */
#ifndef KORIN_SCHED_H
#define KORIN_SCHED_H

#include <stdint.h>

struct vm_space;

enum thread_state { T_NEW, T_READY, T_RUNNING, T_SLEEPING, T_DEAD };
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
	uint64_t kstack;     /* base de la pile du noyau (0 : pile de démarrage) */
	uint64_t kstack_top;
	void *arch_state;    /* état de l'architecture (FPU…) */
	int tid;
	enum thread_state state;
	volatile int on_cpu; /* encore sur un cœur, bascule pas finie */
	int idle_cpu;        /* -1, ou le cœur dont c'est le thread inactif */
	int last_cpu;
	uint64_t wake_ms;
	struct process *proc;
	struct thread *next; /* tous les threads */
	uint64_t user_entry, user_stack;
	char name[24];
};

#define KSTACK_SIZE 16384

extern struct process kernel_process;

/* le contexte de démarrage du cœur devient son thread inactif */
void sched_init_cpu(unsigned cpu, uint64_t stack_top);
struct thread *sched_current(void);
/* thread créé arrêté (T_NEW) ; thread_start le rend prêt */
struct thread *thread_create(struct process *p, const char *name, void (*fn)(void *), void *arg);
void thread_start(struct thread *t);
void schedule(void);
void sched_yield(void);
void sched_sleep(uint64_t ms);
__attribute__((noreturn)) void thread_exit(int code);
/* tout le process courant s'arrête (exit, faute) */
__attribute__((noreturn)) void proc_exit(int code);
void sched_tick(void);      /* minuterie du cœur, interruptions coupées */
void sched_irq_exit(void);  /* fin d'interruption : bascule si le quantum est écoulé */
void sched_reap(void);      /* thread inactif : libère threads et process finis */
/* premier passage d'un thread neuf (appelé par la bascule de l'architecture) */
__attribute__((noreturn)) void sched_thread_start(void (*fn)(void *), void *arg);
/* threads prêts ou actifs, pour la charge */
unsigned sched_running(void);

/* un process depuis une image ELF ; pid ou erreur négative */
int proc_spawn(const char *name, const void *image, uint64_t size);
void proc_list(void);

#endif
