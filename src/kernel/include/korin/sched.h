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
enum { WAKE_OK = 0, WAKE_TIMEOUT = 1, WAKE_KILLED = 2, WAKE_SIGNAL = 3 };

#define NSIG 64

struct sigact {
	uint64_t handler; /* 0 : défaut, 1 : ignoré */
	uint64_t flags;
	uint64_t mask;
};

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
	uint32_t rights;     /* droits de /sys : masque des opérations réservées (bit RU_x) ; RIGHTS_ALL : tous */
	uint32_t rights_seen; /* les opérations réservées déjà notées au journal (rights_used, E33) */
	uint32_t caps;        /* capacités déclarées (opérations RU_x) : la table de vfs au lancement (SPAWN_CAPS) ;
			       * RIGHTS_ALL sans table. Hors d'elles : au journal, refusé si caps_enforce */
	uint32_t caps_seen;   /* les opérations hors capacités déjà notées */
	int uid;             /* à qui sont ses fichiers (0 : system) ; hérité du père (SYS_SETUID, SPAWN_UID) */
	uint64_t flags;      /* PROC_FLAGS */
	uint64_t start_ms;
	char *args;          /* ligne d'arguments copiée chez lui */
	/* fins de threads récentes, pour THREAD_JOIN */
	int join_tid[JOIN_KEEP];
	int join_code[JOIN_KEEP];
	int join_next;
	/* signaux et contrôle des tâches (core/signal.c) */
	struct sigact act[NSIG];
	uint64_t restorer;   /* adresse qui fait SIG_RETURN */
	uint64_t sigpending;
	int sigfrom[NSIG + 1]; /* pid de l'envoyeur d'un signal en attente (si_pid ; 0 : le noyau) */
	int pgid, sid;
	volatile int stop_sig; /* arrêté par ce signal, 0 : non */
	int jobev;           /* pour WAIT étendu : JEV_STOP | signal, JEV_CONT */
	uint64_t alarm_ms, alarm_iv_ms;
	void *ipc_handles;   /* core/ipc.c */
	struct image *image; /* image d'où il a été lancé (core/image.c) */
	char script[128];    /* script qu'un interprète exécute (SYS_PROC_SET_SCRIPT, une fois ; le coffre) */
	int script_set;
	void *evq;           /* file d'événements (core/ws.c) */
	uint64_t svc_n;      /* appels système faits (PROC_STATS) */
	uint64_t svc_tab[128];
	void *ipc_inherit;
	/* capture de syscall (SYS_SYSCALL_CAPTURE, core/signal.c) : cap_n plages [lo, hi) */
	int cap_n;
	int cap_all;          /* sans table de numéros : tous */
	uint64_t cap_lo[4], cap_hi[4];
	uint64_t cap_entry, cap_restorer;
	uint64_t cap_nums[8]; /* 512 bits : numéros capturés */
	/* SVC_MAP_CALLER en cours sur sa mémoire (core/svc.c) : un service épingle ses pages hors des
	 * verrous ; tant qu'il y en a, sched_reap ne le détruit pas et exec ne change pas d'espace.
	 * reaping : choisi par sched_reap (sous sched_lock), plus d'épinglage possible */
	volatile int pins;
	int reaping;
	uint32_t sig_round; /* envoi à un groupe (signal.c) : déjà pris dans ce tour */
	volatile int ipc_transit; /* poignées envoyées par lui, pas encore reçues (ipc.c, TRANSIT_MAX) */
	struct process *next;
};

#define JEV_STOP 0x100
#define JEV_CONT 0x200

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
	void *uregs;         /* trame du mode utilisateur de l'appel en cours */
	int frame_replaced;  /* SIG_RETURN, EXEC : la trame est déjà la bonne */
	uint64_t sigmask, sigpending;
	int sigfrom[NSIG + 1]; /* pid de l'envoyeur (pthread_kill : si_pid) */
	uint64_t saved_mask; /* SIGM_SUSPEND : masque à remettre après le gestionnaire */
	int restore_mask;
	uint64_t sigwait_set; /* SIGM_WAIT : signaux attendus */
	uint64_t alt_sp, alt_size;
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
/* réveille un thread bloqué ou endormi pour un signal (E_INTR) */
void sched_wake_signal(struct thread *t);
void sched_wake_signal_locked(struct thread *t); /* sched_lock pris */
/* premier passage d'un thread neuf (appelé par la bascule de l'architecture) */
__attribute__((noreturn)) void sched_thread_start(void (*fn)(void *), void *arg);
void sched_fork_start(void *frame);
/* thread créé sans fonction : arch_fork_setup prépare sa pile */
struct thread *thread_create_raw(struct process *p, const char *name);
unsigned sched_ready_count(void);
uint64_t sched_switches(void);
/* charge d'un cœur : % de la dernière seconde, ms occupées depuis le démarrage */
void sched_cpu_load(unsigned cpu, uint64_t *load, uint64_t *busy_ms);

/* threads (core/thread.c) : appels système */
struct thread *thread_find(struct process *p, int tid); /* sous sched_lock_take */
uint64_t sched_lock_take(void);
void sched_lock_give(uint64_t f);
struct thread *sched_threads(void);

/* process (core/proc.c) */
struct process *proc_create(const char *name, int parent, uint32_t rights);
/* E33, étape 0 (docs/etude/E33-CARTOGRAPHIE.md) : p a-t-il les droits ? S'il les a et que c'est un process d'une
 * personne (uid > 0 : un programme de session), l'opération réservée est notée au journal, une fois par process */
int rights_used(struct process *p, int op, const char *name);
enum { RU_SVC_REGISTER, RU_SVC_MAP_CALLER, RU_SVC_SIGNAL, RU_CLOCK_SET, RU_BUF_PHYS, RU_DMA_ALLOC, RU_MOD_LAUNCH,
       RU_MOD_READ, RU_BOOT_FILE, RU_BOOT_READY, RU_WS_REGISTER, RU_WS_SIGNAL, RU_CLIP, RU_KIT_REGISTER, RU_DEV_MAP,
       RU_IRQ_BIND, RU_IRQ_MSI, RU_ACPI_TABLE, RU_IO_PORT, RU_POWER, RU_PRIO_MAX, RU_IMAGE_PRELOAD, RU_IMAGE_FORGET,
       RU_PROC_EXE, RU_PROC_STATS, RU_SPAWN_RIGHTS };
/* les droits sont un masque des opérations RU_x (27/11/2026, étape 0 de docs/etude/DURCISSEMENT-N3-N5.md) : RIGHTS_ALL
 * pour les services ; RIGHTS_SVC (SPAWN_SVC) pour ceux qui ne font que servir sous un nom du système (oauthd, gdrivefs :
 * ni matériel, ni disque, ni lancement à droits). PROC_RIGHTS ne rend 1 qu'aux droits complets. */
/* l'uid de bac à sable d'un compte (étape 2 de docs/etude/DURCISSEMENT-N3-N5.md : les process web de Jet) : u | UID_BAC.
 * Un process y entre seul (SETUID vers son propre bac, sans retour) ; son compte peut le signaler et l'arrêter ; vfs,
 * netd, init2, authd lui refusent presque tout */
#define UID_BAC 0x40000000
static inline int uid_bac_de(int bac, int u) { return u >= 0 && !(u & UID_BAC) && bac == (u | UID_BAC); }
#define RIGHTS_ALL 0xFFFFFFFFu
#define RIGHTS_SVC ((1u << RU_SVC_REGISTER) | (1u << RU_SVC_MAP_CALLER))
static inline int rights_full(const struct process *p) { return p->rights == RIGHTS_ALL; }
/* capacités (étape 1 de docs/etude/DURCISSEMENT-N3-N5.md) : des groupes d'opérations, bits 8-23 des options de
 * SPAWN_IMAGE / SPAWN_PRELOADED avec SPAWN_CAPS ; hors d'elles : refusé (caps_enforce), seulement noté avec l'option « caps=log » */
enum { CAP_MMIO, CAP_PORT, CAP_DMA, CAP_IRQ, CAP_ACPI, CAP_SVC, CAP_MAPCALLER, CAP_BOOT, CAP_SYS, CAP_WS, CAP_IMAGE,
       CAP_PROCINFO, CAP_SPAWN, CAP_BUFPHYS, CAP_GROUPS };
uint32_t caps_ops(uint32_t groups);
extern int caps_enforce;
int proc_spawn(const char *name, const void *image, uint64_t size);
int proc_spawn_ex(const char *name, const void *image, uint64_t size, const char *args, int parent, uint32_t rights);
struct process *proc_get(int pid);  /* sous sched_lock_take */
/* le process de ce pid, tenu (pins) : sched_reap ne rend ni lui, ni ses threads finis, avant
 * proc_release ; 0 s'il n'existe pas ou que son démontage a commencé. Sous sched_lock (_locked) ou non.
 * Ne jamais dormir en le tenant : exec et le faucheur attendent que pins revienne à 0. */
struct process *proc_hold(int pid);
struct process *proc_hold_locked(struct process *p);
void proc_release(struct process *p);
int proc_is_child_locked(int pid, int parent);
struct process **proc_list_head(void); /* sous sched_lock_take */
void proc_list(void);
/* appelé par sched_reap pour un process fini, hors verrou */
void proc_finished(struct process *p);

#endif
