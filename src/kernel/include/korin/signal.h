/* Signaux, groupes de process, sessions, contrôle des tâches, minuterie
 * (core/signal.c) : comme src/kernel/signal.asm, jobs.asm et alarm.asm du
 * Pi (docs/ABI.md, 0x2B-0x2F, 0x6D-0x6F). Numéros de newlib. */
#ifndef KORIN_SIGNAL_H
#define KORIN_SIGNAL_H

#include <stdint.h>

struct process;
struct thread;

#define SIGHUP 1
#define SIGINT 2
#define SIGILL 4
#define SIGTRAP 5
#define SIGFPE 8
#define SIGKILL 9
#define SIGBUS 10
#define SIGSEGV 11
#define SIGALRM 14
#define SIGURG 16
#define SIGSTOP 17
#define SIGTSTP 18
#define SIGCONT 19
#define SIGCHLD 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGWINCH 28

#define SA_NOCLDSTOP 1
#define SA_ONSTACK 4
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000

int64_t sys_sig_action(uint64_t sig, uint64_t handler, uint64_t flags, uint64_t mask, uint64_t restorer);
int64_t sys_sig_send(int64_t pid, int64_t tid, uint64_t sig);
int64_t sys_sig_return(void);
int64_t sys_sig_mask(uint64_t how, uint64_t set);
int64_t sys_sig_altstack(uint64_t sp, uint64_t size, uint64_t out);
int64_t sys_setpgid(int64_t pid, int64_t pgid);
int64_t sys_getpgid(int64_t pid);
int64_t sys_setitimer(uint64_t which, int64_t value_us, int64_t interval_us, uint64_t old);

/* envoi depuis le noyau (minuterie, fin d'un fils, faute) */
int signal_send_proc(struct process *p, int sig);
/* retour vers le mode utilisateur (appel système ou interruption) : mort
 * différée, arrêt, livraison d'un signal ; frame = trame de l'architecture */
void signal_user_return(void *frame);
/* faute d'un process : 1 si un gestionnaire la prend (trame préparée) */
int signal_fault(void *frame, int sig, int code, uint64_t addr);
/* fin d'un process : SIGCHLD au père s'il a un gestionnaire */
void signal_child_exit(struct process *p);
/* thread du noyau des minuteries (SETITIMER) */
void signal_alarm_thread(void *arg);

/* fournis par l'architecture : trame d'un gestionnaire, et son retour */
int arch_signal_frame(void *frame, int sig, int code, uint64_t addr, uint64_t handler, uint64_t restorer,
		      uint64_t old_mask, uint64_t stack_top);
int arch_signal_return(void *frame, uint64_t *mask);
uint64_t arch_frame_sp(void *frame);

#endif
