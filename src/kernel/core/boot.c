/* Démarrage (docs/ABI.md 0x42-0x45 ; src/kernel/modules*.asm, bootfiles.asm
 * du Pi). Sur PC, l'initrd tient lieu d'initramfs : ses fichiers sont les
 * modules de démarrage.
 *   MOD_LAUNCH (nom, arguments) : lance un module (droits de SD0:/sys) ;
 *   MOD_READ (nom, tampon, max) : contenu d'un module texte ;
 *   BOOT_FILE (nom, image, taille) : init donne au noyau les fichiers qu'il
 *     attend (SD0:/etc/options.txt) ; nom = 0 : la liste attendue ;
 *   BOOT_READY () : init lève le verrou du démarrage (SD0: est servi) ;
 *     le noyau applique alors les options.
 * Options connues : no_irq (IRQ_BIND : E_NOSYS), no_preload (images : E_NOSYS).
 * Portable. */
#include <korin/arch.h>
#include <korin/boot.h>
#include <korin/errno.h>
#include <korin/initrd.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>

#define NAME 48
#define ARGS 1024
#define BF_MAX (4ULL << 20)

static const char *const expected[] = {"SD0:/etc/options.txt"};
#define NFILES (sizeof expected / sizeof expected[0])

static char *files[NFILES];
static uint64_t file_size[NFILES];
static spinlock_t boot_lock = SPINLOCK_INIT;
static volatile int ready;
static int ready_key;
static char options[1024];

int64_t sys_mod_lancer(uint64_t uname, uint64_t uargs)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	char name[NAME];
	if (copy_string_from_user(name, uname, NAME) <= 0)
		return E_INVAL;
	char *args = 0;
	if (uargs) {
		args = kmalloc(ARGS);
		if (!args)
			return E_NOMEM;
		if (copy_string_from_user(args, uargs, ARGS) < 0)
			args[ARGS - 1] = 0;
	}
	const void *img;
	uint64_t size;
	int64_t r = initrd_find(name, &img, &size) ? E_NOENT : proc_spawn_ex(name, img, size, args, p->pid, 1);
	kfree(args);
	return r;
}

int64_t sys_mod_lire(uint64_t uname, uint64_t buf, uint64_t max)
{
	if (!sched_current()->proc->rights)
		return E_PERM;
	char name[NAME];
	if (copy_string_from_user(name, uname, NAME) <= 0)
		return E_INVAL;
	const void *img;
	uint64_t size;
	if (initrd_find(name, &img, &size))
		return E_NOENT;
	if (size > max)
		size = max;
	return copy_to_user(buf, img, size) ? E_FAULT : (int64_t)size;
}

static int expected_index(const char *name)
{
	for (unsigned i = 0; i < NFILES; i++)
		if (!strcmp(expected[i], name))
			return (int)i;
	return -1;
}

int64_t sys_boot_file(uint64_t uname, uint64_t image, uint64_t size)
{
	if (!uname) { /* la liste des fichiers encore attendus */
		char list[256];
		uint64_t n = 0;
		uint64_t f = spin_lock_irqsave(&boot_lock);
		for (unsigned i = 0; i < NFILES; i++)
			if (!files[i]) {
				uint64_t l = strlen(expected[i]) + 1;
				memcpy(list + n, expected[i], l);
				n += l;
			}
		spin_unlock_irqrestore(&boot_lock, f);
		list[n++] = 0;
		if (n > size)
			n = size;
		return copy_to_user(image, list, n) ? E_FAULT : (int64_t)n;
	}
	if (!sched_current()->proc->rights)
		return E_PERM;
	char name[NAME];
	if (copy_string_from_user(name, uname, NAME) <= 0)
		return E_INVAL;
	int i = expected_index(name);
	if (i < 0 || size == 0 || size > BF_MAX)
		return E_INVAL;
	char *copy = kmalloc(size + 1);
	if (!copy)
		return E_NOMEM;
	if (copy_from_user(copy, image, size)) {
		kfree(copy);
		return E_FAULT;
	}
	copy[size] = 0;
	uint64_t f = spin_lock_irqsave(&boot_lock);
	char *old = files[i];
	files[i] = copy;
	file_size[i] = size;
	spin_unlock_irqrestore(&boot_lock, f);
	kfree(old);
	return 0;
}

/* options : mots de SD0:/etc/options.txt (un par ligne ou séparés par des
 * blancs ; # commente la fin de la ligne) */
static void apply_options(void)
{
	uint64_t f = spin_lock_irqsave(&boot_lock);
	const char *src = files[0];
	uint64_t o = 0;
	options[0] = ' ';
	o = 1;
	for (int comment = 0; src && *src && o + 2 < sizeof options; src++) {
		char c = *src;
		if (c == '#')
			comment = 1;
		if (c == '\n')
			comment = 0;
		if (comment)
			continue;
		if (c == '\r' || c == '\n' || c == '\t' || c == ',')
			c = ' ';
		if (c == ' ' && options[o - 1] == ' ')
			continue;
		options[o++] = c;
	}
	if (options[o - 1] != ' ')
		options[o++] = ' ';
	options[o] = 0;
	spin_unlock_irqrestore(&boot_lock, f);
}

int kernel_option(const char *word)
{
	if (!ready)
		return 0;
	uint64_t n = strlen(word);
	for (const char *p = options; *p; p++) /* « mot » entre deux blancs */
		if (*p == ' ' && !strncmp(p + 1, word, n) && p[n + 1] == ' ')
			return 1;
	return 0;
}

int64_t sys_demarrage_pret(void)
{
	if (!sched_current()->proc->rights)
		return E_PERM;
	apply_options();
	ready = 1;
	sched_wake_all(&ready_key);
	kprintf("Démarrage prêt : SD0: est servi%s%s\n", options[1] ? ", options :" : "", options[1] ? options : "");
	return 0;
}

int boot_wait_ready(uint64_t ms)
{
	uint64_t end = arch_ticks_ms() + ms;
	while (!ready && arch_ticks_ms() < end) {
		sched_prepare_block(end - arch_ticks_ms(), &ready_key);
		if (ready)
			sched_wake(sched_current());
		sched_block();
	}
	return ready;
}
