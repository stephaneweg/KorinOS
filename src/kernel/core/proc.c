/* Process : pids à génération (comme le Pi : génération x 65536 + emplacement
 * + 1, un ancien pid ne désigne jamais un autre process), lancement depuis une
 * image ELF, attente des fins, KILL, listes. Portable. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/elf.h>
#include <korin/errno.h>
#include <korin/hw.h>
#include <korin/image.h>
#include <korin/ipc.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/proc.h>
#include <korin/sched.h>
#include <korin/signal.h>
#include <korin/svc.h>
#include <korin/vm.h>
#include <korin/ws.h>

#define SLOTS 65535
#define EXITS 256
#define ARGS_MAX 1024

static struct process *procs = &kernel_process;
static uint16_t *generation; /* par emplacement */
static struct process **slot_of;
static int slot_hint;
static struct {
	int pid, parent, code, uid;
} exits[EXITS];
static int exit_next;
static int wait_key; /* les threads de WAIT attendent sur cette adresse */

struct process **proc_list_head(void)
{
	return &procs;
}

static int slot(int pid)
{
	return (pid - 1) & 0xFFFF;
}

struct process *proc_get(int pid)
{
	if (pid <= 0 || !slot_of)
		return 0;
	struct process *p = slot_of[slot(pid)];
	return p && p->pid == pid ? p : 0;
}

struct process *proc_hold_locked(struct process *p)
{
	if (!p || p->reaping)
		return 0;
	__atomic_add_fetch(&p->pins, 1, __ATOMIC_ACQ_REL);
	return p;
}

struct process *proc_hold(int pid)
{
	uint64_t f = sched_lock_take();
	struct process *p = proc_hold_locked(proc_get(pid));
	sched_lock_give(f);
	return p;
}

void proc_release(struct process *p)
{
	if (p)
		__atomic_sub_fetch(&p->pins, 1, __ATOMIC_ACQ_REL);
}

/* (sous sched_lock) pid est-il un fils de parent, vivant ou fini depuis peu (exits[]) ? SETPGID : sh fait
 * SETPGID(pid2, pid1) quand pid1 a déjà pu finir (« echo a | cat ») */
int proc_is_child_locked(int pid, int parent)
{
	struct process *c = proc_get(pid);
	if (c)
		return c->parent == parent;
	for (int i = 0; i < EXITS; i++)
		if (exits[i].pid == pid && pid > 0 && exits[i].parent == parent)
			return 1;
	return 0;
}

/* un pid neuf (sous le verrou de l'ordonnanceur) */
static int new_pid(struct process *p)
{
	for (int k = 0; k < SLOTS; k++) {
		int s = (slot_hint + k) % SLOTS;
		if (!slot_of[s]) {
			slot_of[s] = p;
			slot_hint = s + 1;
			return (int)generation[s] * 65536 + s + 1;
		}
	}
	return E_NOMEM;
}

int rights_used(struct process *p, int op, const char *name)
{
	if (op < 0 || op >= 32 || !(p->rights & (1u << op)))
		return 0;
	if (p->uid > 0 && op >= 0 && op < 32 && !(p->rights_seen & (1u << op))) {
		p->rights_seen |= 1u << op;
		kprintf("droits : %s (pid %d, uid %d) : %s\n", p->name, p->pid, p->uid, name);
	}
	return 1;
}

struct process *proc_create(const char *name, int parent, uint32_t rights)
{
	if (!slot_of) {
		slot_of = kzalloc(sizeof(*slot_of) * SLOTS);
		generation = kzalloc(sizeof(*generation) * SLOTS);
		if (!slot_of || !generation)
			return 0;
	}
	struct process *p = kzalloc(sizeof *p);
	if (!p)
		return 0;
	size_t n = strlen(name);
	if (n >= sizeof p->name)
		n = sizeof p->name - 1;
	memcpy(p->name, name, n);
	p->vm = vm_create();
	p->mm = mm_create();
	if (!p->vm || !p->mm) {
		vm_destroy(p->vm);
		kfree(p->mm);
		kfree(p);
		return 0;
	}
	p->parent = parent;
	p->rights = rights;
	p->pgid = p->sid = 0; /* posés plus bas : ceux du père, ou son pid */
	p->start_ms = arch_ticks_ms();
	uint64_t f = sched_lock_take();
	int pid = new_pid(p);
	if (pid > 0) {
		p->pid = pid;
		struct process *dad = proc_get(parent);
		p->pgid = dad ? dad->pgid : pid;
		p->sid = dad ? dad->sid : pid;
		p->uid = dad ? dad->uid : 0; /* (spawn, fork ; exec garde le process) */
		p->state = P_ALIVE;
		p->next = procs;
		procs = p;
	}
	sched_lock_give(f);
	if (pid < 0) {
		vm_destroy(p->vm);
		mm_destroy(p);
		kfree(p);
		return 0;
	}
	return p;
}

static void user_start(void *arg)
{
	(void)arg;
	struct thread *t = sched_current();
	arch_enter_user(t->user_entry, t->user_stack, t->user_arg);
}

/* pile du thread principal : région paresseuse de 64 Mo (comme le Pi) sous USER_STACK_TOP ; le pointeur de pile part
 * d'un point tiré dans le Mo du haut (ASLR, aligné sur 16 octets), les 16 Ko sous lui peuplés ; la ligne d'arguments
 * juste sous ce point */
static int setup_stack(struct process *p, const char *args, uint64_t *sp, uint64_t *argp)
{
	int r = mm_add_region(p->mm, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP, PROT_READ | PROT_WRITE, VMK_STACK);
	if (r)
		return r;
	uint64_t top = USER_STACK_TOP - (random_u64() % (STACK_RND_MAX / 16)) * 16;
	for (uint64_t va = ALIGN_DOWN(top - 16384, PAGE_SIZE); va < top; va += PAGE_SIZE)
		if (mm_fault(p, va, 1, 0))
			return E_NOMEM;
	*argp = 0;
	*sp = top - 16;
	if (args && *args) {
		size_t n = strlen(args);
		if (n >= ARGS_MAX)
			n = ARGS_MAX - 1;
		uint64_t a = ALIGN_DOWN(top - n - 1, 16);
		/* écrit par la carte de la mémoire : l'espace n'est pas actif ici */
		for (size_t i = 0; i <= n; i++) {
			uint64_t va = a + i;
			uint8_t *pg = arch_phys_to_virt(vm_translate(p->vm, ALIGN_DOWN(va, PAGE_SIZE)));
			pg[va & (PAGE_SIZE - 1)] = i < n ? (uint8_t)args[i] : 0;
		}
		*argp = a;
		*sp = a - 16;
	}
	return 0;
}

/* stdio : entrée, sortie, erreur du fils (params de SPAWN_IMAGE : poignées de vfs ou de vterm, 0 :
 * celle du père, -1 : aucune ; stdio = 0 : toutes héritées) : dans l'avis de naissance, et leur
 * masque dans PROC_FLAGS (bits 0-2 : pas la console), comme proc_spawn_elf du Pi */
static int spawn_common(const char *name, const void *image, uint64_t size, struct image *im, const char *args,
			int parent, uint32_t rights, const int64_t *stdio, int uid)
{
	struct process *p = proc_create(name, parent, rights);
	if (!p) {
		image_put(im);
		return E_NOMEM;
	}
	if (uid >= 0)
		p->uid = uid; /* SPAWN_UID (avant son premier thread) */
	uint64_t mask = sched_current()->proc->flags & 7; /* (ceux de l'appelant, hérités) */
	for (int i = 0; stdio && i < 3; i++)
		if (stdio[i])
			mask = stdio[i] == -1 ? mask & ~(1ULL << i) : mask | (1ULL << i);
	p->flags = (p->flags & ~7ULL) | mask;
	p->image = im; /* rendue à la fin du process */
	uint64_t entry, end;
	uint64_t sp, argp;
	int r = im ? image_map(im, p->vm, p->mm, &entry, &end) : elf_load(p->vm, p->mm, image, size, &entry, &end);
	if (r == 0) {
		mm_set_brk(p->mm, end);
		r = setup_stack(p, args, &sp, &argp);
	}
	struct thread *t = r ? 0 : thread_create(p, p->name, user_start, 0);
	if (!t) {
		uint64_t f = sched_lock_take();
		p->state = P_ZOMBIE; /* sans thread : libéré par sched_reap */
		p->exit_code = r ? r : E_NOMEM;
		sched_lock_give(f);
		return r ? r : E_NOMEM;
	}
	t->user_entry = entry;
	t->user_stack = sp;
	t->user_arg = argp;
	struct process *caller = sched_current()->proc;
	if (parent && caller->pid == parent)
		ipc_inherit(caller, p); /* HANDLE_INHERIT du père */
	svc_notify_birth(p->pid, parent, stdio);
	thread_start(t);
	return p->pid;
}

int proc_spawn_ex(const char *name, const void *image, uint64_t size, const char *args, int parent, uint32_t rights)
{
	return spawn_common(name, image, size, 0, args, parent, rights, 0, -1);
}

int proc_spawn(const char *name, const void *image, uint64_t size)
{
	return proc_spawn_ex(name, image, size, 0, 0, RIGHTS_ALL);
}

/* appelé par sched_reap, hors verrou : le process n'a plus de thread */
void proc_finished(struct process *p)
{
	svc_proc_exit(p);
	hw_proc_exit(p);
	image_proc_exit(p);
	ws_proc_exit(p);
	ipc_proc_exit(p);
	mm_destroy(p);
	vm_destroy(p->vm);
	p->vm = 0;
	kfree(p->args);
	uint64_t f = sched_lock_take();
	slot_of[slot(p->pid)] = 0;
	/* modulo 32767 : generation * 65536 + slot + 1 reste un int positif (pid_t, MSG_PID sur 32 bits) */
	generation[slot(p->pid)] = (uint16_t)((generation[slot(p->pid)] + 1) % 32767);
	exits[exit_next].pid = p->pid;
	exits[exit_next].parent = p->parent;
	exits[exit_next].code = p->exit_code;
	exits[exit_next].uid = p->uid;
	exit_next = (exit_next + 1) % EXITS;
	sched_lock_give(f);
	sched_wake_all(&wait_key);
	console_color(CON_DIM);
	kprintf("[process %d (%s) fini, code %d]\n", p->pid, p->name, p->exit_code);
	console_color(CON_NORMAL);
	svc_notify_fin(p->pid, p->exit_code);
	signal_child_exit(p);
	kfree(p);
}

/* ---- appels système */

#define WAIT_EX_TAG 0x57414954ULL
#define WEX_NOHANG 1

int64_t sys_wait(int64_t pid, uint64_t opt, uint64_t out)
{
	int ext = (opt >> 32) == WAIT_EX_TAG;
	struct process *me = sched_current()->proc;
	for (;;) {
		uint64_t f = sched_lock_take();
		struct process *p = proc_get((int)pid);
		int found = p != 0, code = 0, done = 0, puid = p ? p->uid : -1, parent = p ? p->parent : 0;
		if (!p)
			for (int i = 0; i < EXITS; i++)
				if (exits[i].pid == pid && pid > 0) {
					found = done = 1;
					code = exits[i].code;
					puid = exits[i].uid;
					parent = exits[i].parent;
				}
		if (!found) {
			sched_lock_give(f);
			return E_SRCH;
		}
		/* attendre un process (et lire son code de sortie) : son père, son compte, ou l'uid 0 ; un autre
		 * compte suivait la fin de sudo, de login... (N25, E43) */
		if (parent != me->pid && puid != me->uid && me->uid != 0) {
			sched_lock_give(f);
			return E_PERM;
		}
		if (done) {
			sched_lock_give(f);
			if (!ext)
				return code;
			int64_t c = code;
			if (out && copy_to_user(out, &c, 8))
				return E_FAULT;
			return 1; /* WR_EXITED */
		}
		if (ext && p && p->parent == sched_current()->proc->pid && p->jobev) {
			int ev = p->jobev;
			if (((ev & JEV_STOP) && (opt & 2)) || ((ev & JEV_CONT) && (opt & 8))) {
				p->jobev = 0;
				sched_lock_give(f);
				int64_t c = ev & 0xFF;
				if (out && copy_to_user(out, &c, 8))
					return E_FAULT;
				return (ev & JEV_STOP) ? 2 : 3; /* WR_STOPPED, WR_CONTINUED */
			}
		}
		if (ext && (opt & WEX_NOHANG)) {
			sched_lock_give(f);
			return 0; /* WR_NONE */
		}
		sched_lock_give(f);
		sched_prepare_block(20, &wait_key); /* un autre que le père : revoit toutes les 20 ms */
		if (sched_block() == WAKE_KILLED)
			return E_INTR;
	}
}

int64_t sys_kill(int64_t pid)
{
	struct process *me = sched_current()->proc;
	uint64_t f = sched_lock_take();
	struct process *p = proc_hold_locked(proc_get((int)pid)); /* (tenu : pas rendu pendant l'envoi) */
	int ok = p && p->state != P_ZOMBIE;
	int same = p && (me->uid == 0 || p->uid == me->uid);
	sched_lock_give(f);
	if (ok && same)
		sched_kill_process(p, -9);
	proc_release(p);
	if (!ok)
		return E_SRCH;
	if (!same)
		return E_PERM; /* un autre utilisateur, un service : seul l'uid 0 (§2.2 de l'étude) */
	return 0;
}

/* entrée de PROCLIST / PROC_INFO : 64 octets */
static void fill_entry(struct process *p, uint64_t e[8])
{
	memset(e, 0, 64);
	e[0] = (uint64_t)p->pid;
	e[1] = (uint64_t)p->threads;
	e[2] = mm_heap_kb(p);
	memcpy(&e[3], p->name, 32);
	e[7] = p->state == P_ZOMBIE ? 3 : 0;
}

int64_t sys_proclist_from(uint64_t buf, uint64_t max, uint64_t from)
{
	if (max > 4096)
		max = 4096;
	uint64_t *tmp = kmalloc(max * 64 + 64);
	if (!tmp)
		return E_NOMEM;
	uint64_t n = 0;
	uint64_t f = sched_lock_take();
	for (uint64_t s = from; s < SLOTS && n < max && slot_of; s++) {
		struct process *p = slot_of[s];
		if (p)
			fill_entry(p, &tmp[n++ * 8]);
	}
	sched_lock_give(f);
	int64_t r = copy_to_user(buf, tmp, n * 64) ? E_FAULT : (int64_t)n;
	kfree(tmp);
	return r;
}

int64_t sys_proc_info(int64_t pid, uint64_t out)
{
	uint64_t e[8];
	uint64_t f = sched_lock_take();
	struct process *p = pid ? proc_get((int)pid) : sched_current()->proc;
	if (p)
		fill_entry(p, e);
	sched_lock_give(f);
	if (!p)
		return E_SRCH;
	return copy_to_user(out, e, 64) ? E_FAULT : 0;
}

int64_t sys_proc_rights(int64_t pid)
{
	uint64_t f = sched_lock_take();
	struct process *p = proc_get((int)pid);
	int64_t r = !p ? E_SRCH : rights_full(p) ? 1 : p->rights ? 2 : 0; /* (2 : droits de service seulement) */
	sched_lock_give(f);
	return r;
}

/* SYS_SETUID : seul un process d'uid 0 change d'uid (login, authd) ; vfs est prévenu, qui garde l'uid
 * de chaque pid (l'en-tête des messages n'a pas de place pour lui) */
int64_t sys_setuid(int64_t uid)
{
	struct process *me = sched_current()->proc;
	if (uid < 0 || uid > 0x7FFFFFFF)
		return E_INVAL;
	if (me->uid != 0)
		return me->uid == uid ? 0 : E_PERM;
	me->uid = (int)uid;
	svc_notify_setuid(me->pid, (int)uid);
	return 0;
}

/* SYS_PROC_SETUID : un process d'uid 0 (authd) change l'uid d'un autre (sudo, une fois le mot de passe
 * vérifié) ; le process garde ses droits sur le matériel (aucun : sudo est dans /bin) */
int64_t sys_proc_setuid(int64_t pid, int64_t uid)
{
	struct process *me = sched_current()->proc;
	if (uid < 0 || uid > 0x7FFFFFFF)
		return E_INVAL;
	if (me->uid != 0)
		return E_PERM;
	uint64_t f = sched_lock_take();
	struct process *p = proc_get((int)pid);
	int ok = p && p->state != P_ZOMBIE;
	if (ok)
		p->uid = (int)uid;
	sched_lock_give(f);
	if (!ok)
		return E_SRCH;
	svc_notify_setuid((int)pid, (int)uid);
	return 0;
}

/* SYS_PROC_EXE : l'identité d'un programme pour le coffre (docs/etude/COFFRE.md §2.1) : le chemin déclaré de son
 * image, l'empreinte SHA-256 de son contenu (le coffre relit le fichier et compare : le chemin est ainsi
 * vérifié), le script déclaré par un interprète */
struct pex {
	uint32_t flags, uid;
	uint8_t hash[32];
	char exe[128];
	char script[128];
};
_Static_assert(sizeof(struct pex) == 296, "PEX_SIZE");

int64_t sys_proc_exe(int64_t pid, uint64_t out)
{
	struct pex *x = kzalloc(sizeof *x);
	if (!x)
		return E_NOMEM;
	uint64_t f = sched_lock_take();
	struct process *p = proc_get((int)pid);
	struct process *me = sched_current()->proc;
	if (p && me->uid != 0 && p->uid != me->uid && !rights_used(me, RU_PROC_EXE, "PROC_EXE (autre uid)")) { /* (son compte, l'uid 0, les droits : E43) */
		sched_lock_give(f);
		kfree(x);
		return E_PERM;
	}
	struct image *im = 0;
	if (p && p->state != P_ZOMBIE) {
		x->uid = (uint32_t)p->uid;
		x->flags = rights_full(p) ? 2 : 0;
		if (p->script_set) {
			x->flags |= 4;
			memcpy(x->script, p->script, sizeof x->script);
		}
		im = p->image;
		image_hold(im); /* (img_lock sous le verrou de l'ordonnanceur ; exec et la fin changent p->image sous lui) */
	}
	sched_lock_give(f);
	if (!p) {
		kfree(x);
		return E_SRCH;
	}
	if (im) {
		image_identity(im, x->exe, sizeof x->exe, x->hash);
		if (image_is_sys(im))
			x->flags |= 8; /* (chargée par le système : le chemin déclaré est celui du fichier) */
		image_put(im);
		x->flags |= 1;
	}
	int64_t r = copy_to_user(out, x, sizeof *x) ? E_FAULT : 0;
	kfree(x);
	return r;
}

/* SYS_PROC_SET_SCRIPT : un interprète dit quel script il exécute, une fois (avant le code du script) */
int64_t sys_proc_set_script(uint64_t upath)
{
	struct process *me = sched_current()->proc;
	char path[128];
	if (copy_string_from_user(path, upath, sizeof path) <= 0)
		return E_INVAL;
	path[sizeof path - 1] = 0;
	uint64_t f = sched_lock_take();
	int64_t r = E_PERM;
	if (!me->script_set) {
		memcpy(me->script, path, sizeof path);
		me->script_set = 1;
		r = 0;
	}
	sched_lock_give(f);
	return r;
}

int64_t sys_proc_uid(int64_t pid)
{
	uint64_t f = sched_lock_take();
	struct process *p = proc_get((int)pid);
	int64_t r = p ? p->uid : E_SRCH;
	sched_lock_give(f);
	return r;
}

/* SPAWN_UID (options de SPAWN_IMAGE et SPAWN_PRELOADED) : l'uid du fils, bits 32-63, dans *uid (-1 : celui
 * du père) ; E_PERM si l'appelant n'est pas d'uid 0 */
static int spawn_uid(const struct process *me, uint64_t opt, int *uid)
{
	*uid = -1;
	if (!(opt & SPAWN_UID))
		return 0;
	if (me->uid != 0)
		return E_PERM;
	*uid = (int)(opt >> 32) & 0x7FFFFFFF;
	return 0;
}

int64_t sys_getppid(void)
{
	struct process *me = sched_current()->proc;
	uint64_t f = sched_lock_take();
	int64_t r = me->parent && proc_get(me->parent) ? me->parent : 1;
	sched_lock_give(f);
	return r;
}

/* SPAWN_IMAGE (image, taille, params 48 o {nom, arguments, options, entrée,
 * sortie, erreur}) ; les entrées / sorties sont l'affaire de vfs */
int64_t sys_spawn_image(uint64_t image, uint64_t size, uint64_t params)
{
	struct process *me = sched_current()->proc;
	uint64_t pr[6] = {0};
	if (params && copy_from_user(pr, params, 48))
		return E_FAULT;
	uint32_t rights = 0;
	if (pr[2] & 1) { /* les droits du père (complets : lui seul a RU_SPAWN_RIGHTS), réduits par SPAWN_SVC */
		if (!rights_used(me, RU_SPAWN_RIGHTS, "SPAWN avec droits"))
			return E_PERM;
		rights = me->rights & ((pr[2] & SPAWN_SVC) ? RIGHTS_SVC : RIGHTS_ALL);
	}
	int uid;
	if (spawn_uid(me, pr[2], &uid))
		return E_PERM;
	char name[PROC_NAME] = "?";
	if (pr[0] && copy_string_from_user(name, pr[0], sizeof name) < 0)
		name[sizeof name - 1] = 0;
	char *args = 0;
	if (pr[1]) {
		args = kmalloc(ARGS_MAX);
		if (!args)
			return E_NOMEM;
		if (copy_string_from_user(args, pr[1], ARGS_MAX) < 0)
			args[ARGS_MAX - 1] = 0;
	}
	if (size == 0 || size > (256ULL << 20)) {
		kfree(args);
		return E_INVAL;
	}
	void *img = kmalloc(size);
	if (!img) {
		kfree(args);
		return E_NOMEM;
	}
	int64_t r = copy_from_user(img, image, size);
	if (r == 0)
		r = spawn_common(name, img, size, 0, args, me->pid, rights, (const int64_t *)&pr[3], uid);
	kfree(img);
	kfree(args);
	return r;
}

/* ---- fork : copie du process, un seul thread (l'appelant), rax = 0 dans le fils */

#define SVC_OP_FORK 0xFFFC

int64_t sys_fork(void)
{
	struct thread *t = sched_current();
	struct process *p = t->proc;
	if (p->rights)
		return E_PERM; /* droits sur le matériel : pas de fork (comme le Pi) */
	struct process *c = proc_create(p->name, p->pid, 0);
	if (!c)
		return E_NOMEM;
	int r = mm_fork(p, c);
	if (r == 0)
		r = ipc_fork(p, c);
	struct thread *ct = r ? 0 : thread_create_raw(c, t->name);
	if (!ct) {
		uint64_t f = sched_lock_take();
		c->state = P_ZOMBIE;
		c->exit_code = E_NOMEM;
		sched_lock_give(f);
		return E_NOMEM;
	}
	memcpy(c->act, p->act, sizeof c->act);
	/* l'image du père : ses pages de code sont aussi projetées dans le fils (partagées, sans
	 * compte de pages) ; sans cette référence, la fin ou l'exec du père rendait une image
	 * passagère et ses pages pendant que le fils les exécutait encore */
	c->image = p->image;
	image_hold(c->image);
	memcpy(c->script, p->script, sizeof c->script); /* (le fils d'un interprète garde son script) */
	c->script_set = p->script_set;
	c->restorer = p->restorer;
	c->flags = p->flags;
	/* la capture de syscall : même mémoire, même hôte */
	memcpy(c->cap_lo, p->cap_lo, sizeof c->cap_lo);
	memcpy(c->cap_hi, p->cap_hi, sizeof c->cap_hi);
	memcpy(c->cap_nums, p->cap_nums, sizeof c->cap_nums);
	c->cap_entry = p->cap_entry;
	c->cap_restorer = p->cap_restorer;
	c->cap_all = p->cap_all;
	c->cap_n = p->cap_n;
	ct->priority = t->priority;
	ct->sigmask = t->sigmask;
	ct->alt_sp = t->alt_sp;
	ct->alt_size = t->alt_size;
	arch_fork_setup(ct, t->uregs);
	svc_notify(SVF_BIRTHS, SVC_OP_FORK, c->pid, p->pid);
	thread_start(ct);
	return c->pid;
}

/* ---- exec : un programme neuf dans le même process (même pid, même père,
 * mêmes services) ; seulement depuis une image en mémoire (EXP_IMAGE) tant
 * que les images préchargées ne sont pas faites (E_NOENT : AppKit lira le
 * fichier) */

#define INHERIT_MARKS_MAX 255 /* (IPC_INHERIT_MAX) */

static int64_t sys_exec_image(struct thread *t, struct process *p, uint64_t path, uint64_t size, const uint64_t *pr,
			      const uint32_t *marks, int nmarks);

int64_t sys_exec(uint64_t path, uint64_t size, uint64_t params)
{
	struct thread *t = sched_current();
	struct process *p = t->proc;
	uint64_t pr[9] = {0};
	if (!params || copy_from_user(pr, params, 72))
		return E_FAULT;
	if ((pr[2] & 1) || p->rights)
		return E_PERM;
	/* EXP_MARKS : marques des poignées que le nouveau programme retrouve (HANDLE_LIST) */
	uint32_t *marks = 0;
	int nmarks = (int)pr[8];
	if (pr[8] > INHERIT_MARKS_MAX)
		return E_INVAL;
	if (nmarks) {
		marks = kmalloc((uint64_t)nmarks * 8);
		if (!marks)
			return E_NOMEM;
		int64_t e = copy_from_user(marks, pr[7], (uint64_t)nmarks * 8) ? E_FAULT
			    : ipc_exec_marks_ok(marks, nmarks) ? 0 : E_BADF;
		if (e) {
			kfree(marks);
			return e;
		}
	}
	int64_t r = sys_exec_image(t, p, path, size, pr, marks, nmarks);
	kfree(marks);
	return r;
}

static int64_t sys_exec_image(struct thread *t, struct process *p, uint64_t path, uint64_t size, const uint64_t *pr,
			      const uint32_t *marks, int nmarks)
{
	struct image *im = 0;
	if (!pr[6]) {
		char ipath[IMAGE_PATH_MAX];
		if (copy_string_from_user(ipath, path, sizeof ipath) <= 0)
			return E_INVAL;
		im = image_get(ipath, size, 0, p->uid);
		if (!im)
			return E_NOENT; /* AppKit construit l'image */
	} else if (size == 0 || size > (256ULL << 20))
		return E_INVAL;
	char name[PROC_NAME];
	int newname = pr[0] && copy_string_from_user(name, pr[0], sizeof name) > 0;
	char *args = 0;
	if (pr[1]) {
		args = kmalloc(ARGS_MAX);
		if (!args)
			return E_NOMEM;
		if (copy_string_from_user(args, pr[1], ARGS_MAX) < 0)
			args[ARGS_MAX - 1] = 0;
	}
	void *img = im ? 0 : kmalloc(size);
	int64_t r = im ? 0 : img ? copy_from_user(img, pr[6], size) : E_NOMEM;
	/* espace neuf construit à côté : une erreur laisse le process intact */
	struct process tmp = {0};
	tmp.vm = r ? 0 : vm_create();
	tmp.mm = r ? 0 : mm_create();
	uint64_t entry = 0, end = 0, sp = 0, argp = 0;
	if (!r && (!tmp.vm || !tmp.mm))
		r = E_NOMEM;
	if (!r)
		r = im ? image_map(im, tmp.vm, tmp.mm, &entry, &end) : elf_load(tmp.vm, tmp.mm, img, size, &entry, &end);
	if (!r) {
		mm_set_brk(tmp.mm, end);
		r = setup_stack(&tmp, args, &sp, &argp);
	}
	kfree(img);
	kfree(args);
	if (r) {
		vm_destroy(tmp.vm);
		mm_free(tmp.mm);
		image_put(im);
		return r;
	}
	/* point de non-retour : les autres threads meurent */
	uint64_t f = sched_lock_take();
	for (struct thread *x = sched_threads(); x; x = x->next)
		if (x->proc == p && x != t && x->state != T_DEAD)
			x->kill_pending = 1, x->kill_code = 0;
	sched_lock_give(f);
	for (struct thread *x;;) {
		f = sched_lock_take();
		x = 0;
		for (struct thread *y = sched_threads(); y && !x; y = y->next)
			if (y->proc == p && y != t && (y->state != T_DEAD || y->on_cpu))
				x = y;
		if (x) /* (sous le verrou : x ne peut pas être rendu entre-temps) */
			sched_wake_signal_locked(x);
		sched_lock_give(f);
		if (!x)
			break;
		sched_sleep(1);
	}
	/* un service épingle encore des pages de l'ancien espace (SVC_MAP_CALLER, core/svc.c) : il
	 * finit ; les requêtes des threads morts sont abandonnées, il n'en commence plus */
	while (__atomic_load_n(&p->pins, __ATOMIC_ACQUIRE))
		sched_sleep(1);
	struct vm_space *old_vm = p->vm;
	struct mm *old_mm = p->mm;
	f = sched_lock_take();
	p->vm = tmp.vm;
	p->mm = tmp.mm;
	sched_lock_give(f);
	vm_activate(p->vm);
	vm_destroy(old_vm);
	mm_free(old_mm);
	f = sched_lock_take(); /* (SYS_PROC_EXE lit p->image sous ce verrou) */
	struct image *old_im = p->image;
	p->image = im;
	p->script_set = 0; /* (le nouveau programme dira son script) */
	memset(p->script, 0, sizeof p->script);
	sched_lock_give(f);
	image_put(old_im);
	/* signaux : gestionnaires remis par défaut, ignorés gardés */
	for (int i = 0; i < NSIG; i++)
		if (p->act[i].handler > 1)
			p->act[i] = (struct sigact){0, 0, 0};
	p->restorer = 0;
	p->cap_n = 0; /* (plus de capture de syscall : l'hôte est parti avec l'ancien programme) */
	t->alt_sp = t->alt_size = 0;
	if (newname) {
		memcpy(p->name, name, PROC_NAME);
		memcpy(t->name, name, THREAD_NAME - 1);
		t->name[THREAD_NAME - 1] = 0;
	}
	p->flags |= 0x200; /* PROC_EXEC */
	ipc_exec_marks(marks, nmarks);
	/* entrées / sorties (EXP_STDIN.. : 0 inchangée, -1 aucune) : masque de PROC_FLAGS (bits 0-2),
	 * puis l'avis à vfs s'il y a un changement (exec_stdio du Pi) */
	int64_t io[3] = {(int64_t)pr[3], (int64_t)pr[4], (int64_t)pr[5]};
	if (io[0] || io[1] || io[2]) {
		uint64_t mask = p->flags & 7;
		for (int i = 0; i < 3; i++)
			if (io[i])
				mask = io[i] == -1 ? mask & ~(1ULL << i) : mask | (1ULL << i);
		p->flags = (p->flags & ~7ULL) | mask;
		svc_notify_exec(p->pid, io);
	}
	arch_exec_frame(t->uregs, entry, sp, argp);
	t->frame_replaced = 1;
	return 0;
}

void proc_wake_waiters(void)
{
	sched_wake_all(&wait_key);
}

int64_t sys_spawn_preloaded(uint64_t upath, uint64_t fsize, uint64_t params)
{
	struct process *me = sched_current()->proc;
	uint64_t pr[6] = {0};
	if (params && copy_from_user(pr, params, 48))
		return E_FAULT;
	uint32_t rights = 0;
	if (pr[2] & 1) { /* les droits du père (complets : lui seul a RU_SPAWN_RIGHTS), réduits par SPAWN_SVC */
		if (!rights_used(me, RU_SPAWN_RIGHTS, "SPAWN avec droits"))
			return E_PERM;
		rights = me->rights & ((pr[2] & SPAWN_SVC) ? RIGHTS_SVC : RIGHTS_ALL);
	}
	int uid;
	if (spawn_uid(me, pr[2], &uid))
		return E_PERM;
	char path[IMAGE_PATH_MAX];
	if (copy_string_from_user(path, upath, sizeof path) <= 0)
		return E_INVAL;
	struct image *im = image_get(path, fsize, rights != 0, uid < 0 ? me->uid : uid);
	if (!im)
		return E_NOENT;
	char name[PROC_NAME];
	const char *base = path;
	for (const char *c = path; *c; c++)
		if (*c == '/')
			base = c + 1;
	ksnprintf(name, sizeof name, "%s", base);
	if (pr[0] && copy_string_from_user(name, pr[0], sizeof name) < 0)
		name[sizeof name - 1] = 0;
	char *args = 0;
	if (pr[1]) {
		args = kmalloc(ARGS_MAX);
		if (!args) {
			image_put(im);
			return E_NOMEM;
		}
		if (copy_string_from_user(args, pr[1], ARGS_MAX) < 0)
			args[ARGS_MAX - 1] = 0;
	}
	int64_t r = spawn_common(name, 0, 0, im, args, me->pid, rights, (const int64_t *)&pr[3], uid);
	kfree(args);
	return r;
}

void proc_list(void)
{
	static const char *const st[] = {"neuf", "prêt", "actif", "dort", "attend", "fini"};
	uint64_t f = sched_lock_take();
	kprintf("  pid  nom              threads\n");
	for (struct process *p = procs; p; p = p->next) {
		if (p == &kernel_process && p->threads > 4)
			kprintf("  %3d  %-16s %d (dont %u inactifs, un par cœur)\n", p->pid, p->name, p->threads,
				arch_cpu_count());
		else
			kprintf("  %3d  %-16s %d%s\n", p->pid, p->name, p->threads, p->state == P_ZOMBIE ? " (fini)" : "");
		for (struct thread *t = sched_threads(); t; t = t->next) {
			if (t->proc != p || (t->idle_cpu >= 0 && t->state != T_RUNNING))
				continue;
			if (t->state == T_RUNNING)
				kprintf("         tid %d %s : actif sur le cœur %d\n", t->tid, t->name, t->last_cpu);
			else
				kprintf("         tid %d %s : %s\n", t->tid, t->name, st[t->state]);
		}
	}
	sched_lock_give(f);
}
