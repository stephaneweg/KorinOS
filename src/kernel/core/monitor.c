/* Moniteur du noyau : petite ligne de commande pour les premiers démarrages,
 * en attendant les process et le shell. Portable. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/initrd.h>
#include <korin/lib.h>
#include <korin/pmm.h>
#include <korin/sched.h>

#define LINE_MAX 128

static void status_tick(void)
{
	static uint64_t last = ~0ULL;
	uint64_t s = arch_ticks_ms() / 1000;
	if (s == last)
		return;
	last = s;
	char buf[32];
	ksnprintf(buf, sizeof buf, "%02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
	console_status(buf);
}

static void cmd_help(void)
{
	kprintf("  help     cette aide\n"
		"  info     processeur, ACPI, minuterie\n"
		"  mem      mémoire physique\n"
		"  ps       process et threads\n"
		"  ls       programmes de l'initrd\n"
		"  run NOM [ARGS]  lancer un programme de l'initrd (droits de /sys)\n"
		"  urun NOM [ARGS] le lancer sans droits (fork permis)\n"
		"  uptime   temps depuis le démarrage\n"
		"  clear    effacer l'écran\n"
		"  reboot   redémarrer\n"
		"  poweroff éteindre (ACPI S5)\n");
}

static void cmd_mem(void)
{
	uint64_t t = pmm_total_pages(), f = pmm_free_pages();
	kprintf("  mémoire gérée : %lu Mo, libre : %lu Mo (%lu pages de 4 Ko)\n",
		t * PAGE_SIZE >> 20, f * PAGE_SIZE >> 20, f);
	uint64_t a = pmm_alloc(), b = pmm_alloc();
	kprintf("  essai : deux pages à %lx et %lx", a, b);
	if (a)
		pmm_free(a);
	if (b)
		pmm_free(b);
	kprintf(", rendues\n");
}

static void run(char *line)
{
	while (*line == ' ')
		line++;
	if (!*line)
		return;
	if (!strcmp(line, "help"))
		cmd_help();
	else if (!strcmp(line, "info"))
		arch_print_info();
	else if (!strcmp(line, "mem"))
		cmd_mem();
	else if (!strcmp(line, "ps"))
		proc_list();
	else if (!strcmp(line, "ls"))
		initrd_list();
	else if (!strncmp(line, "run ", 4) || !strncmp(line, "urun ", 5)) {
		int rights = line[0] == 'r';
		char *name = line + (rights ? 4 : 5);
		while (*name == ' ')
			name++;
		char *args = name; /* « run NOM arguments » : la suite est la ligne d'arguments */
		while (*args && *args != ' ')
			args++;
		if (*args)
			*args++ = 0;
		const void *img;
		uint64_t size;
		if (initrd_find(name, &img, &size))
			kprintf("  %s : absent de l'initrd\n", name);
		else {
			int pid = proc_spawn_ex(name, img, size, *args ? args : 0, 0, rights ? RIGHTS_ALL : 0);
			if (pid < 0)
				kprintf("  %s : erreur %d\n", name, pid);
			else
				kprintf("  %s : process %d\n", name, pid);
		}
	}
	else if (!strcmp(line, "uptime")) {
		uint64_t ms = arch_ticks_ms();
		kprintf("  %lu,%03lu s\n", ms / 1000, ms % 1000);
	} else if (!strcmp(line, "clear"))
		console_clear();
	else if (!strcmp(line, "reboot")) {
		kprintf("  redémarrage\n");
		arch_reboot();
	} else if (!strcmp(line, "poweroff")) {
		kprintf("  extinction (sans vider les volumes : nvme stop d'abord)\n");
		arch_power_off();
	} else {
		console_color(CON_WARN);
		kprintf("  commande inconnue : %s (help)\n", line);
		console_color(CON_NORMAL);
	}
}

void monitor_run(void *arg)
{
	(void)arg;
	char line[LINE_MAX];
	size_t len = 0;

	kprintf("Tapez help pour la liste des commandes.\n");
	for (;;) {
		console_color(CON_ACCENT);
		kprintf("korin> ");
		console_color(CON_NORMAL);
		len = 0;
		for (;;) {
			int c = arch_getc();
			if (c < 0) {
				status_tick();
				sched_sleep(10);
				continue;
			}
			if (c == '\n') {
				console_putc('\n');
				arch_debug_putc('\n');
				break;
			}
			if (c == '\b') {
				if (len) {
					len--;
					console_putc('\b');
				}
				continue;
			}
			if (c < 0x20 || len + 1 >= LINE_MAX)
				continue;
			line[len++] = (char)c;
			console_putc((unsigned char)c);
		}
		line[len] = 0;
		run(line);
	}
}
