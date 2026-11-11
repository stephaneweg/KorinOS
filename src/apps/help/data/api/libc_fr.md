# La libc et la couche POSIX

Un programme C ou C++ de Korin OS s'écrit comme sous Unix : `printf`, `fopen`, `malloc`, `pthread_create`, `socket`, `std::thread`, `std::filesystem`... Trois bibliothèques, liées **en statique** dans chaque programme, le permettent :

| Bibliothèque | Ce qu'elle donne |
|---|---|
| **newlib 4.4** | la bibliothèque C standard : `stdio.h`, `stdlib.h`, `string.h`, `math.h` (`-lm`), `time.h`, `ctype.h`, `wchar.h`, `regex.h`, `getopt.h`... |
| **libstdc++** (GCC 14.2) | la bibliothèque C++ jusqu'à C++20 : conteneurs, chaînes, exceptions, `std::thread`, `std::mutex`, `std::filesystem`, `std::async`, `std::chrono` |
| **libposix** | la couche POSIX de Korin OS : fichiers et dossiers, processus, threads, mémoire, signaux, sockets, tubes, IPC ; reprise de celle d'Onyx et écrite sur ce que le système offre (appels du noyau, AppKit, le service de fichiers vfs, NetKit) |

Le système, lui, est en assembleur ; la libc ne s'occupe que des programmes. Cette page dit ce qui marche comme sous Unix, ce qui diffère et ce qui manque. POSIX n'est pas un but en soi : la couche grandit selon ce qu'il faut pour porter des applications et des bibliothèques.

## Compiler

Sous Korin OS, GCC est dans `/gcc` :

```sh
/gcc/bin/gcc -O2 hello.c -o /tmp/hello
/gcc/bin/g++ -std=gnu++20 -O2 hello.cpp -o /tmp/hello
pibuild bin /home/projets/hello hello
```

Sans autre option, le point d'entrée, libposix, newlib et libstdc++ sont liés d'office ; le résultat est un programme statique de Korin OS. `pibuild` (voir [Une application](app_fr.md)) ajoute les en-têtes des kits (`-I/gcc/include/kits`) et toutes leurs bibliothèques. Les macros `__unix__` et `__armpi__` sont définies : un programme portable peut tester `__armpi__`.

## Les chemins

Un chemin de Korin OS nomme un **volume** : `SD0:/apps/help.app`, `RAM0:/tmp/x`, `USB0:/photos`. Les formes de POSIX marchent aussi :

| Écrit | Désigne |
|---|---|
| `SD0:/a/b` | le fichier `a/b` de la carte SD (partition FAT32) |
| `RAM0:/x` | un volume en mémoire, effacé à l'arrêt |
| `x`, `./x`, `../x` | relatif au dossier courant (`chdir`, `getcwd`) |
| `/tmp/x` | `RAM0:/tmp/x` : les fichiers temporaires (`mkstemp`, `tmpfile`) vont en mémoire |
| `/gcc`, `/bin`, `/apps`, `/lib`, `/dotnet`, `/etc` | des **chemins virtuels** : `SD0:/gcc`... quel que soit le dossier courant (table de `SD0:/etc/paths.txt`) |
| `/autre` | la racine du **volume courant** : depuis `SD0:/home`, `/docs` est `SD0:/docs` |
| `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`, `/dev/tty` | des pseudo-fichiers |

`realpath` rend la forme complète (`SD0:/a/b`) ; il garde tels quels les chemins virtuels (`/gcc/bin/gcc`), qui n'ont pas de `:` et passent donc dans `PATH`. `HOME` vaut `SD0:/` et `PATH` `/bin:/gcc/bin` par défaut.

> **Remarque** : la partition `SD1:` de la carte est celle de l'utilisateur ; un programme n'y écrit que ce que l'utilisateur lui demande.

## Fichiers et dossiers

`open`, `read`, `write`, `lseek`, `pread`, `pwrite`, `close`, `dup`, `dup2`, `fcntl`, `stat`, `fstat`, `ftruncate`, `unlink`, `mkdir`, `rmdir`, `rename`, `opendir`, `readdir`, `nftw`, `fnmatch`, et tout `stdio.h`. Les écarts :

- **une seule date** par fichier : `st_mtime`, `st_atime` et `st_ctime` valent la même, à 2 s près sur la carte (FAT) ;
- ni droits ni propriétaires : `chmod`, `chown` sont sans effet, l'identité est unique (uid 0) ;
- pas de liens : `link`, `symlink` rendent `ENOSYS`, `readlink` `EINVAL` (sauf `/proc/self/exe`) ;
- `rename` : immédiat sur un même volume ; d'un volume à l'autre, un fichier est recopié, un dossier rend `EXDEV` ;
- **64 fichiers ouverts** au plus par programme ; un chemin fait au plus 254 octets, volume compris ;
- pas de verrous (`flock`, `F_SETLK` rendent 0 sans rien faire) ; `statvfs` rend des valeurs fixes.

## Processus

| Fonction | Sur Korin OS |
|---|---|
| `posix_spawn`, `posix_spawnp` | lancent un programme ; `adddup2`, `addopen` (descripteurs 0 à 2), `addclose`, `addchdir_np` |
| `waitpid`, `wait` | attendent un fils ; `WEXITSTATUS` |
| `exec*` | lance le programme, l'attend, puis finit avec son code (le pid change) |
| `system`, `popen` | lancent la commande sans shell : le premier mot est le programme |
| `fork`, `vfork` | `ENOSYS` : utilisez `posix_spawn` |
| `getpid`, `kill` | oui ; `getppid` rend 1 |
| `getenv`, `setenv`, `environ` | oui ; `argv` et l'environnement passent au fils sans limite de taille |

Un nom seul (`ls`) est cherché dans `SD0:/bin`, puis dans `SD0:/apps/<nom>.app/main`. Un fils reçoit à leur numéro les canaux locaux et la mémoire partagée sans `FD_CLOEXEC`, mais pas les fichiers, tubes ou sockets au-delà de 2.

## Threads

`pthread_create`, `join`, `detach`, mutex, variables de condition, verrous en lecture et écriture, `pthread_once`, clés, `pthread_kill`, sémaphores (`sem_*`), et en C++ `std::thread`, `std::mutex`, `std::condition_variable`, `thread_local`, `std::async`. Les threads se répartissent sur les quatre cœurs du Pi 4.

Une pile de thread fait **256 Ko** par défaut (`pthread_attr_setstacksize` jusqu'à 64 Mo), sous une page de garde ; le thread principal a **64 Mo**. Ces piles ne coûtent que ce qui est touché.

## Mémoire

`malloc` et `new` passent par le tas de newlib. `mmap` anonyme réserve une région peuplée à la demande (plusieurs Go possibles, `PROT_EXEC` compris) ; `munmap`, `mprotect`, `madvise`, `mincore` sont là. Un fichier projeté en lecture (64 Ko à 128 Mo) est partagé entre les programmes qui le projettent, en copie sur écriture ; `MAP_SHARED` en écriture d'un fichier rend `ENOTSUP`. La mémoire partagée passe par `memfd_create` ou `shm_open` puis `mmap (MAP_SHARED)`.

Propres à Korin OS (`<sys/mman.h>`) : `file_pin (chemin)` lit un fichier une fois et le garde en mémoire sans programme, `file_unpin`, `file_pinned`.

## Signaux

`sigaction` (avec `SA_SIGINFO`), `signal`, `raise`, `kill`, `pthread_kill`, masques par thread, `sigaltstack`, `sigpending`, `sigsuspend`, `pause`, `sigwait`. Une faute (`SIGSEGV`, `SIGBUS`...) se rattrape, avec `si_addr` et un `ucontext_t` modifiable. `SIGCHLD` arrive à la fin d'un fils, seulement si un gestionnaire est installé.

Ce qui diffère : `SA_RESTART` est sans effet (un `sleep`, un `poll`, un `recv` interrompus rendent `EINTR`, une lecture de fichier va au bout) ; `SIGPIPE` n'est jamais levé ; `timer_create` rend `ENOSYS`.

## Réseau, tubes et IPC

- **Sockets BSD** IPv4 (`socket`, `connect`, `accept`, `getaddrinfo`, `poll`, `select`, `epoll`) : écrits sur [NetKit](netkit_fr.md), qui en dit les écarts.
- **Tubes** : `pipe`, avec `O_NONBLOCK` ; ce sont aussi l'entrée et la sortie standard d'un programme lancé du Terminal.
- **Canaux locaux** : `socketpair (AF_UNIX, SOCK_STREAM | SOCK_SEQPACKET | SOCK_DGRAM)`, `sendmsg`, `recvmsg`, `SCM_RIGHTS` (pour passer un canal ou de la mémoire partagée) ; pas de socket `AF_UNIX` nommé.
- **Temps** : `time`, `gettimeofday`, `clock_gettime` (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`), `nanosleep`, `usleep` ; le fuseau est UTC.
- **Aléa** : `getrandom`, `/dev/urandom`.

## Modules chargés (dlopen)

Les programmes sont statiques, mais un programme peut charger des modules `.so` avec `dlopen`, `dlsym`, `dlclose`, `dladdr`, `dlerror`. Il doit être relié en **hôte** : `-rdynamic` exporte toute la libc vers ses modules, et réserve 32 Ko par thread pour leurs variables `__thread`. Un module ne porte pas sa propre libc : il prend celle de l'hôte.

```sh
gcc -specs=$P/posix.specs -rdynamic hote.c -o hote
gcc -specs=$P/posix.specs -fPIC -shared module.c -o module.so
g++ -specs=$P/posix.specs -fPIC -shared -nostdlib++ module.cpp -o module.so   # module C++
```

Un hôte qui charge des modules C++ ajoute `$P/dlhost-cxx.ld` à sa ligne de liens (avec `g++`). Un nom sans dossier est cherché dans `LD_LIBRARY_PATH`, puis le `RUNPATH` du module qui le demande (`-Wl,-rpath,'$ORIGIN'` : son propre dossier), puis `/usr/lib` ; jamais dans `SD0:/lib`, réservé aux kits. Les constructeurs tournent au chargement, les exceptions C++ traversent l'hôte et les modules. `dlclose` ne décharge jamais. Le détail : `docs/POSIX.md`, section « dlopen ».

## Ce qui manque

`fork` ; les liens ; les droits ; IPv6 ; les sockets `AF_UNIX` nommés ; `getifaddrs` ; `timer_create` ; le déchargement des modules (`dlclose` les garde) ; `/proc` (sauf `/proc/self/exe`) ; les fuseaux horaires autres qu'UTC. `syscall` ne connaît que `gettid`, `getrandom`, `membarrier`.

## AppKit et les kits vus du C

**AppKit** est le kit en assembleur qui fait tourner tout programme de Korin OS : console, chaînes, tas, client de fichiers (descripteurs, tubes, entrée et sortie standard), lancement des programmes, chargement des kits, mutex. Un programme en assembleur l'appelle directement. Un programme C, **non** : il n'y a pas d'en-tête `appkit_abi.h`. La libc s'en sert pour lui :

| Le programme C appelle | La libc passe par |
|---|---|
| `open`, `read`, `fopen`, `opendir`, `stat`... | le client de fichiers d'AppKit, puis le service vfs |
| `posix_spawn`, `exec*`, `system` | le lancement d'AppKit (`f_spawn_file`) |
| `<kit>_kit_open ()` | le chargement des kits d'AppKit (`f_kit_load`) |
| `pthread_*`, `mmap`, signaux, temps | les appels du noyau, directement |
| `socket`, `getaddrinfo` | NetKit |

Les autres kits ont leur en-tête C, généré depuis leur `.abi` : `uikit.h`, `audiokit_abi.h`, `mediakit.h`, `gpukit_abi.h`, `emukit.h`, `netkit_abi.h`, `gpiokit_abi.h`, `archivekit_abi.h`, `documentkit_abi.h`, `printerkit.h`. Chaque fonction y prend des `long`. En dessous, `<armpi/kit.h>` ouvre n'importe quel kit par son nom et donne une fonction par son rang (sa ligne dans le `.abi`, à partir de 0) :

| Fonction | Résultat |
|---|---|
| `armpi_kit_open (nom, version minimale)` | la table du kit (`SD0:/lib/<nom>.so`), ou NULL |
| `armpi_kit_fn (table, rang)` | la fonction, ou NULL si le kit en a moins |
| `armpi_kit_version (table)` | le nombre de fonctions du kit |

## Exemple : fichiers et chemins

Écrit un fichier dans `/tmp`, le relit, montre les chemins tels que le système les voit et liste les applications installées.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

int main (void)
{
	char cwd[PATH_MAX], real[PATH_MAX];
	if (getcwd (cwd, sizeof cwd))
		printf ("dossier courant : %s\n", cwd);

	mkdir ("/tmp/demo", 0755);
	FILE *f = fopen ("/tmp/demo/notes.txt", "w");
	if (!f)
	{
		perror ("fopen");
		return 1;
	}
	for (int i = 1; i <= 3; i++)
		fprintf (f, "ligne %d\n", i);
	fclose (f);

	struct stat st;
	if (stat ("/tmp/demo/notes.txt", &st) == 0 && realpath ("/tmp/demo/notes.txt", real))
		printf ("%s : %ld octets\n", real, (long) st.st_size);
	f = fopen ("/tmp/demo/notes.txt", "r");
	char line[64];
	while (f && fgets (line, sizeof line, f))
		printf ("  lu : %s", line);
	if (f)
		fclose (f);

	if (realpath ("/gcc/bin/gcc", real))
		printf ("/gcc/bin/gcc reste %s (chemin virtuel)\n", real);

	DIR *d = opendir ("SD0:/apps");
	if (d)
	{
		struct dirent *e;
		int n = 0;
		while ((e = readdir (d)) != NULL)
			if (strstr (e->d_name, ".app") && n++ < 10)
				printf ("  %s\n", e->d_name);
		closedir (d);
		printf ("... %d applications\n", n);
	}
	unlink ("/tmp/demo/notes.txt");
	rmdir ("/tmp/demo");
	return 0;
}
```

## Exemple : threads

Quatre threads se partagent un calcul ; un mutex protège le total, une variable de condition prévient le thread principal.

```c
#include <stdio.h>
#include <pthread.h>

#define WORKERS	4
#define N	4000000L

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done = PTHREAD_COND_INITIALIZER;
static double total;
static int finished;

static void *work (void *arg)
{
	long k = (long) arg;
	double s = 0;
	for (long i = k; i < N; i += WORKERS)		/* pi / 4 = 1 - 1/3 + 1/5 - ... */
		s += (i % 2 ? -1.0 : 1.0) / (2 * i + 1);
	pthread_mutex_lock (&mu);
	total += s;
	finished++;
	pthread_cond_signal (&done);
	pthread_mutex_unlock (&mu);
	return NULL;
}

int main (void)
{
	pthread_t t[WORKERS];
	for (long k = 0; k < WORKERS; k++)
		if (pthread_create (&t[k], NULL, work, (void *) k) != 0)
		{
			printf ("pthread_create a echoue\n");
			return 1;
		}
	pthread_mutex_lock (&mu);
	while (finished < WORKERS)		/* reveils parasites possibles : on reteste */
		pthread_cond_wait (&done, &mu);
	pthread_mutex_unlock (&mu);
	for (int k = 0; k < WORKERS; k++)
		pthread_join (t[k], NULL);
	printf ("pi ~ %.8f (%d threads)\n", 4 * total, WORKERS);
	return 0;
}
```

## Exemple : lancer un programme

Lance `echo` avec sa sortie redirigée dans un fichier de `/tmp`, attend sa fin (et la voit arriver par `SIGCHLD`), puis lit le fichier en le projetant en mémoire.

```c
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;
static volatile sig_atomic_t children;

static void on_child (int sig)
{
	(void) sig;
	children++;
}

int main (void)
{
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_child;
	sigaction (SIGCHLD, &sa, NULL);

	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_addopen (&fa, 1, "/tmp/out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	char *argv[] = { "echo", "Bonjour", "depuis", "un", "fils", NULL };
	pid_t pid;
	int e = posix_spawnp (&pid, "echo", &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	if (e != 0)
	{
		printf ("posix_spawnp : %s\n", strerror (e));
		return 1;
	}
	int status;
	waitpid (pid, &status, 0);
	printf ("fils %d fini, code %d, SIGCHLD recu %d fois\n", (int) pid, WEXITSTATUS (status), (int) children);

	int fd = open ("/tmp/out.txt", O_RDONLY);
	struct stat st;
	if (fd >= 0 && fstat (fd, &st) == 0 && st.st_size > 0)
	{
		char *p = mmap (NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (p != MAP_FAILED)
		{
			printf ("sa sortie : %.*s", (int) st.st_size, p);
			munmap (p, (size_t) st.st_size);
		}
	}
	if (fd >= 0)
		close (fd);
	return 0;
}
```

## Exemple : mémoire et fautes rattrapées

Réserve une grande région sans la payer, puis protège une page et rattrape la faute qu'elle provoque : le gestionnaire rend la page accessible et l'instruction reprend.

```c
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

static char *g_page;
static long g_size;
static volatile int g_faults;

static void on_fault (int sig, siginfo_t *si, void *ctx)
{
	(void) sig;
	(void) ctx;
	char *a = (char *) si->si_addr;
	if (a >= g_page && a < g_page + g_size)
	{
		g_faults++;
		mprotect (g_page, (size_t) g_size, PROT_READ | PROT_WRITE);	/* l'instruction est rejouee */
		return;
	}
	_exit (1);
}

int main (void)
{
	g_size = sysconf (_SC_PAGESIZE);
	size_t big = (size_t) 1 << 30;					/* 1 Go reserve, rien n'est pris */
	char *r = mmap (NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED)
		return 1;
	for (size_t off = 0; off < big; off += big / 8)
		r[off] = 1;						/* 8 pages touchees seulement */
	printf ("1 Go reserve, 8 pages touchees\n");
	munmap (r, big);

	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_fault;
	sa.sa_flags = SA_SIGINFO;
	sigaction (SIGSEGV, &sa, NULL);

	g_page = mmap (NULL, (size_t) g_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (g_page == MAP_FAILED)
		return 1;
	strcpy (g_page, "ecrit apres la faute");			/* faute, rattrapee */
	printf ("%s (%d faute)\n", g_page, (int) g_faults);
	munmap (g_page, (size_t) g_size);
	return 0;
}
```

## Exemple : C++ moderne

`std::filesystem`, `std::async` et les exceptions marchent comme ailleurs.

```cpp
#include <cstdio>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::uintmax_t folder_size (const fs::path &p)
{
	std::uintmax_t n = 0;
	for (auto &e : fs::recursive_directory_iterator (p, fs::directory_options::skip_permission_denied))
		if (e.is_regular_file ())
			n += e.file_size ();
	return n;
}

int main ()
{
	std::vector<std::string> dirs = { "/lib", "/etc", "/bin" };
	std::vector<std::future<std::uintmax_t>> parts;
	for (auto &d : dirs)
		parts.push_back (std::async (std::launch::async, folder_size, fs::path (d)));
	for (size_t i = 0; i < dirs.size (); i++)
		std::printf ("%-5s %8ju Ko\n", dirs[i].c_str (), parts[i].get () / 1024);
	try
	{
		auto n = fs::file_size ("/tmp/absent");
		std::printf ("%ju octets ?\n", n);
	}
	catch (const fs::filesystem_error &e)
	{
		std::printf ("exception attendue : %s\n", e.what ());
	}
	return 0;
}
```

## Pièges

- **Pas de `fork`** : un code Unix qui fait `fork` puis `exec` se réécrit avec `posix_spawn`.
- **`system ("a | b")`** ne passe pas par un shell : pas de tubes, pas de redirections. Lancez `sh -c "..."` si vous en avez besoin, ou `posix_spawn` avec des actions.
- **`/tmp` est en mémoire** : rapide, mais perdu à l'arrêt. Ce qui doit rester va sur `SD0:`.
- **Un seul fuseau, UTC** : `localtime` rend la même heure que `gmtime`.
- **`connect` attend toujours**, même en non bloquant : dans une app graphique, un thread à part.
- **Un programme graphique** reste un programme C ordinaire : la fenêtre vient d'[UIKit](uikit_fr.md), la libc fait le reste.

## Voir aussi

- [Une application](app_fr.md) et [le squelette](skeleton_fr.md) : construire une app avec `pibuild`.
- [NetKit](netkit_fr.md) : le réseau sous les sockets.
- [Écrire un kit](kit_fr.md), [IPC](ipc_fr.md), [un service](service_fr.md).
