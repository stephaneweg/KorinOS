/* Process, threads et ordonnanceur : tourniquet préemptif à priorités sur
 * tous les cœurs (une file commune, un thread inactif par cœur). Portable :
 * l'architecture fournit la pile, l'état FPU et la bascule. */
#ifndef KORIN_SCHED_H
#define KORIN_SCHED_H

#include <stdint.h>

#include <korin/spinlock.h>

struct vm_space;
struct mm;

enum thread_state { T_NEW, T_READY, T_RUNNING, T_SLEEPING, T_BLOCKED, T_DEAD };
enum proc_state { P_ALIVE, P_ZOMBIE };

/* raison du réveil d'un thread bloqué */
enum { WAKE_OK = 0, WAKE_TIMEOUT = 1, WAKE_KILLED = 2 };

#define PROC_NAME 32
#define THREAD_NAME 24
#define JOIN_KEEP 16

struct process {
	int pid;
	int parent;          /* pid du père (0 : le noyau) */
	char name[PROC_NAME];
	struct vm_space *vm; /* 0 : process du noyau */
	struct mm *mm;
	int threads;
	enum proc_state state;
	int exit_code;
	int rights;          /* droits de SD0:/sys (services, matériel) */
	uint64_t flags;      /* PROC_FLAGS */
	uint64_t start_ms;
	char *args;          /* ligne d'arguments copiée chez lui */
	/* fins de threads récentes, pour THREAD_JOIN */
	int join_tid[JOIN_KEEP];
	int join_code[JOIN_KEEP];
	int join_next;
	struct process *next;
};

struct thread {
	uint64_t arch_sp;    /* pile du noyau sauvée par la bascule */
	uint64_t kstack;     /* base de la pile du noyau (0 : pile de démarrage) */
	uint64_t kstack_top;
	void *arch_state;    /* état de l'architecture (FPU, TLS…) */
	int tid;
	enum thread_state state;
	volatile int on_cpu; /* encore sur un cœur, bascule pas finie */
	int idle_cpu;        /* -1, ou le cœur dont c'est le thread inactif */
	int last_cpu;
	int priority;        /* THREAD_PRIO_* */
	int points;          /* âge depuis le dernier passage */
	uint64_t wake_ms;    /* échéance (sommeil, attente), 0 : aucune */
	int wake_reason;
	volatile int kill_pending;
	int kill_code;
	uint64_t runtime_ms;
	uint64_t ran_since;
	struct process *proc;
	struct thread *next;      /* tous les threads */
	struct thread *wait_next; /* file d'attente d'un objet */
	void *wait_obj;
	uint64_t user_entry, user_stack, user_arg, user_tls;
	char name[THREAD_NAME];
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
/* SLEEP : 0, ou E_INTR si le thread est tué pendant */
int sched_sleep(uint64_t ms);

/* Attente sur un objet : sous le verrou de l'objet, sched_prepare_block
 * (échéance en ms, 0 : sans fin) ; le verrou rendu, sched_block rend la
 * raison du réveil (WAKE_*). sched_wake réveille un thread bloqué. */
void sched_prepare_block(uint64_t timeout_ms, void *obj);
int sched_block(void);
void sched_wake(struct thread *t);
void sched_wake_all(void *obj); /* tous les threads bloqués sur obj */
/* KILL, fin de process : chaque thread meurt à son retour en mode utilisateur */
void sched_kill_process(struct process *p, int code);
/* THREAD_KILL : différé jusqu'à son retour en mode utilisateur */
void sched_kill_thread(struct thread *t, int code);

__attribute__((noreturn)) void thread_exit(int code);
/* tout le process courant s'arrête (exit, faute) */
__attribute__((noreturn)) void proc_exit(int code);
void sched_tick(void);      /* minuterie du cœur, interruptions coupées */
void sched_irq_exit(void);  /* fin d'interruption : bascule si le quantum est écoulé */
void sched_reap(void);      /* thread inactif : libère threads et process finis */
/* retour vers le mode utilisateur : meurt si THREAD_KILL le demande */
void sched_user_return(void);
/* premier passage d'un thread neuf (appelé par la bascule de l'architecture) */
__attribute__((noreturn)) void sched_thread_start(void (*fn)(void *), void *arg);
unsigned sched_ready_count(void);
uint64_t sched_switches(void);

/* threads (core/thread.c) : appels système */
struct thread *thread_find(struct process *p, int tid); /* sous sched_lock_take */
uint64_t sched_lock_take(void);
void sched_lock_give(uint64_t f);
struct thread *sched_threads(void);

/* process (core/proc.c) */
struct process *proc_create(const char *name, int parent, int rights);
int proc_spawn(const char *name, const void *image, uint64_t size);
int proc_spawn_ex(const char *name, const void *image, uint64_t size, const char *args, int parent, int rights);
struct process *proc_get(int pid);  /* sous sched_lock_take */
struct process **proc_list_head(void); /* sous sched_lock_take */
void proc_list(void);
/* appelé par sched_reap pour un process fini, hors verrou */
void proc_finished(struct process *p);

#endif
