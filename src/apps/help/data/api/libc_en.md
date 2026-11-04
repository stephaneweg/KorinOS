# The libc and the POSIX layer

A C or C++ program for Korin OS is written as under Unix: `printf`, `fopen`, `malloc`, `pthread_create`, `socket`, `std::thread`, `std::filesystem`... Three libraries, linked **statically** into every program, make this possible:

| Library | What it provides |
|---|---|
| **newlib 4.4** | the standard C library: `stdio.h`, `stdlib.h`, `string.h`, `math.h` (`-lm`), `time.h`, `ctype.h`, `wchar.h`, `regex.h`, `getopt.h`... |
| **libstdc++** (GCC 14.2) | the C++ library up to C++20: containers, strings, exceptions, `std::thread`, `std::mutex`, `std::filesystem`, `std::async`, `std::chrono` |
| **libposix** | the Korin OS POSIX layer: files and folders, processes, threads, memory, signals, sockets, pipes, IPC; taken from Onyx and written on what the system offers (kernel calls, AppKit, the vfs file service, NetKit) |

The system itself is in assembly; the libc is only for programs. This page says what works as under Unix, what differs and what is missing. POSIX is not a goal in itself: the layer grows with what is needed to port applications and libraries.

## Compiling

Under Korin OS, GCC lives in `/gcc`:

```sh
/gcc/bin/gcc -O2 hello.c -o /tmp/hello
/gcc/bin/g++ -std=gnu++20 -O2 hello.cpp -o /tmp/hello
pibuild bin /home/projets/hello hello
```

With no other option, the entry point, libposix, newlib and libstdc++ are linked automatically; the result is a static Korin OS program. `pibuild` (see [An application](app_en.md)) adds the kit headers (`-I/gcc/include/kits`) and all their libraries. The macros `__unix__` and `__armpi__` are defined: a portable program can test `__armpi__`.

## Paths

A Korin OS path names a **volume**: `SD0:/apps/help.app`, `RAM0:/tmp/x`, `USB0:/photos`. POSIX forms work too:

| Written | Means |
|---|---|
| `SD0:/a/b` | the file `a/b` of the SD card (FAT32 partition) |
| `RAM0:/x` | a volume in memory, wiped at shutdown |
| `x`, `./x`, `../x` | relative to the current folder (`chdir`, `getcwd`) |
| `/tmp/x` | `RAM0:/tmp/x`: temporary files (`mkstemp`, `tmpfile`) go to memory |
| `/gcc`, `/bin`, `/apps`, `/lib`, `/dotnet`, `/etc` | **virtual paths**: `SD0:/gcc`... whatever the current folder (table in `SD0:/etc/paths.txt`) |
| `/other` | the root of the **current volume**: from `SD0:/home`, `/docs` is `SD0:/docs` |
| `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`, `/dev/tty` | pseudo-files |

`realpath` returns the full form (`SD0:/a/b`); it keeps virtual paths as they are (`/gcc/bin/gcc`), which have no `:` and therefore fit in `PATH`. `HOME` is `SD0:/` and `PATH` is `/bin:/gcc/bin` by default.

> **Note**: the card's `SD1:` partition belongs to the user; a program writes there only what the user asks it to.

## Files and folders

`open`, `read`, `write`, `lseek`, `pread`, `pwrite`, `close`, `dup`, `dup2`, `fcntl`, `stat`, `fstat`, `ftruncate`, `unlink`, `mkdir`, `rmdir`, `rename`, `opendir`, `readdir`, `nftw`, `fnmatch`, and all of `stdio.h`. The differences:

- **one date** per file: `st_mtime`, `st_atime` and `st_ctime` are the same, to within 2 s on the card (FAT);
- no permissions or owners: `chmod`, `chown` have no effect, there is a single identity (uid 0);
- no links: `link`, `symlink` return `ENOSYS`, `readlink` `EINVAL` (except `/proc/self/exe`);
- `rename`: immediate on the same volume; across volumes, a file is copied, a folder returns `EXDEV`;
- **64 open files** at most per program; a path is at most 254 bytes, volume included;
- no locks (`flock`, `F_SETLK` return 0 and do nothing); `statvfs` returns fixed values.

## Processes

| Function | On Korin OS |
|---|---|
| `posix_spawn`, `posix_spawnp` | start a program; `adddup2`, `addopen` (descriptors 0 to 2), `addclose`, `addchdir_np` |
| `waitpid`, `wait` | wait for a child; `WEXITSTATUS` |
| `exec*` | starts the program, waits for it, then exits with its code (the pid changes) |
| `system`, `popen` | start the command without a shell: the first word is the program |
| `fork`, `vfork` | `ENOSYS`: use `posix_spawn` |
| `getpid`, `kill` | yes; `getppid` returns 1 |
| `getenv`, `setenv`, `environ` | yes; `argv` and the environment pass to the child with no size limit |

A bare name (`ls`) is looked up in `SD0:/bin`, then in `SD0:/apps/<name>.app/main`. A child receives, at their numbers, the local channels and shared memory without `FD_CLOEXEC`, but not files, pipes or sockets beyond 2.

## Threads

`pthread_create`, `join`, `detach`, mutexes, condition variables, read-write locks, `pthread_once`, keys, `pthread_kill`, semaphores (`sem_*`), and in C++ `std::thread`, `std::mutex`, `std::condition_variable`, `thread_local`, `std::async`. Threads are spread over the four cores of the Pi 4.

A thread stack is **256 KB** by default (`pthread_attr_setstacksize` up to 64 MB), under a guard page; the main thread has **64 MB**. These stacks only cost what is touched.

## Memory

`malloc` and `new` go through the newlib heap. Anonymous `mmap` reserves a region filled on demand (several GB possible, `PROT_EXEC` included); `munmap`, `mprotect`, `madvise`, `mincore` are there. A file mapped for reading (64 KB to 128 MB) is shared between the programs that map it, copy-on-write; writable `MAP_SHARED` of a file returns `ENOTSUP`. Shared memory goes through `memfd_create` or `shm_open` then `mmap (MAP_SHARED)`.

Specific to Korin OS (`<sys/mman.h>`): `file_pin (path)` reads a file once and keeps it in memory with no program attached, `file_unpin`, `file_pinned`.

## Signals

`sigaction` (with `SA_SIGINFO`), `signal`, `raise`, `kill`, `pthread_kill`, per-thread masks, `sigaltstack`, `sigpending`, `sigsuspend`, `pause`, `sigwait`. A fault (`SIGSEGV`, `SIGBUS`...) can be caught, with `si_addr` and a modifiable `ucontext_t`. `SIGCHLD` arrives when a child ends, only if a handler is installed.

What differs: `SA_RESTART` has no effect (an interrupted `sleep`, `poll` or `recv` returns `EINTR`, a file read goes to the end); `SIGPIPE` is never raised; `timer_create` returns `ENOSYS`.

## Network, pipes and IPC

- **BSD sockets**, IPv4 (`socket`, `connect`, `accept`, `getaddrinfo`, `poll`, `select`, `epoll`): built on [NetKit](netkit_en.md), which lists the differences.
- **Pipes**: `pipe`, with `O_NONBLOCK`; they are also the standard input and output of a program started from the Terminal.
- **Local channels**: `socketpair (AF_UNIX, SOCK_STREAM | SOCK_SEQPACKET | SOCK_DGRAM)`, `sendmsg`, `recvmsg`, `SCM_RIGHTS` (to pass a channel or shared memory); no named `AF_UNIX` socket.
- **Time**: `time`, `gettimeofday`, `clock_gettime` (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`), `nanosleep`, `usleep`; the time zone is UTC.
- **Randomness**: `getrandom`, `/dev/urandom`.

## What is missing

`fork`; links; permissions; IPv6; named `AF_UNIX` sockets; `getifaddrs`; `timer_create`; `dlopen` (programs are static: it fails cleanly); `/proc` (except `/proc/self/exe`); time zones other than UTC. `syscall` only knows `gettid`, `getrandom`, `membarrier`.

## AppKit and the kits seen from C

**AppKit** is the assembly kit that runs every Korin OS program: console, strings, heap, file client (descriptors, pipes, standard input and output), starting programs, loading kits, mutexes. An assembly program calls it directly. A C program does **not**: there is no `appkit_abi.h` header. The libc uses it on the program's behalf:

| The C program calls | The libc goes through |
|---|---|
| `open`, `read`, `fopen`, `opendir`, `stat`... | the AppKit file client, then the vfs service |
| `posix_spawn`, `exec*`, `system` | AppKit's program launcher (`f_spawn_file`) |
| `<kit>_kit_open ()` | AppKit's kit loader (`f_kit_load`) |
| `pthread_*`, `mmap`, signals, time | kernel calls, directly |
| `socket`, `getaddrinfo` | NetKit |

The other kits have their C header, generated from their `.abi`: `uikit.h`, `audiokit_abi.h`, `mediakit.h`, `gpukit_abi.h`, `emukit.h`, `netkit_abi.h`, `gpiokit_abi.h`, `archivekit_abi.h`, `documentkit_abi.h`, `printerkit.h`. Every function there takes `long` arguments. Underneath, `<armpi/kit.h>` opens any kit by name and gives a function by its rank (its line in the `.abi`, from 0):

| Function | Result |
|---|---|
| `armpi_kit_open (name, minimum version)` | the kit's table (`SD0:/lib/<name>.so`), or NULL |
| `armpi_kit_fn (table, rank)` | the function, or NULL if the kit has fewer |
| `armpi_kit_version (table)` | the kit's number of functions |

## Example: files and paths

Writes a file in `/tmp`, reads it back, shows paths as the system sees them and lists the installed applications.

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
		printf ("current folder: %s\n", cwd);

	mkdir ("/tmp/demo", 0755);
	FILE *f = fopen ("/tmp/demo/notes.txt", "w");
	if (!f)
	{
		perror ("fopen");
		return 1;
	}
	for (int i = 1; i <= 3; i++)
		fprintf (f, "line %d\n", i);
	fclose (f);

	struct stat st;
	if (stat ("/tmp/demo/notes.txt", &st) == 0 && realpath ("/tmp/demo/notes.txt", real))
		printf ("%s: %ld bytes\n", real, (long) st.st_size);
	f = fopen ("/tmp/demo/notes.txt", "r");
	char line[64];
	while (f && fgets (line, sizeof line, f))
		printf ("  read: %s", line);
	if (f)
		fclose (f);

	if (realpath ("/gcc/bin/gcc", real))
		printf ("/gcc/bin/gcc stays %s (virtual path)\n", real);

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

## Example: threads

Four threads share a computation; a mutex protects the total, a condition variable tells the main thread.

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
			printf ("pthread_create failed\n");
			return 1;
		}
	pthread_mutex_lock (&mu);
	while (finished < WORKERS)		/* spurious wake-ups are possible: test again */
		pthread_cond_wait (&done, &mu);
	pthread_mutex_unlock (&mu);
	for (int k = 0; k < WORKERS; k++)
		pthread_join (t[k], NULL);
	printf ("pi ~ %.8f (%d threads)\n", 4 * total, WORKERS);
	return 0;
}
```

## Example: starting a program

Starts `echo` with its output redirected to a file in `/tmp`, waits for it to end (and sees it end through `SIGCHLD`), then reads the file by mapping it into memory.

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
	char *argv[] = { "echo", "Hello", "from", "a", "child", NULL };
	pid_t pid;
	int e = posix_spawnp (&pid, "echo", &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	if (e != 0)
	{
		printf ("posix_spawnp: %s\n", strerror (e));
		return 1;
	}
	int status;
	waitpid (pid, &status, 0);
	printf ("child %d ended, code %d, SIGCHLD received %d time(s)\n", (int) pid, WEXITSTATUS (status), (int) children);

	int fd = open ("/tmp/out.txt", O_RDONLY);
	struct stat st;
	if (fd >= 0 && fstat (fd, &st) == 0 && st.st_size > 0)
	{
		char *p = mmap (NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (p != MAP_FAILED)
		{
			printf ("its output: %.*s", (int) st.st_size, p);
			munmap (p, (size_t) st.st_size);
		}
	}
	if (fd >= 0)
		close (fd);
	return 0;
}
```

## Example: memory and caught faults

Reserves a large region without paying for it, then protects a page and catches the fault it causes: the handler makes the page accessible and the instruction runs again.

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
		mprotect (g_page, (size_t) g_size, PROT_READ | PROT_WRITE);	/* the instruction is replayed */
		return;
	}
	_exit (1);
}

int main (void)
{
	g_size = sysconf (_SC_PAGESIZE);
	size_t big = (size_t) 1 << 30;					/* 1 GB reserved, nothing taken */
	char *r = mmap (NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED)
		return 1;
	for (size_t off = 0; off < big; off += big / 8)
		r[off] = 1;						/* only 8 pages touched */
	printf ("1 GB reserved, 8 pages touched\n");
	munmap (r, big);

	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_fault;
	sa.sa_flags = SA_SIGINFO;
	sigaction (SIGSEGV, &sa, NULL);

	g_page = mmap (NULL, (size_t) g_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (g_page == MAP_FAILED)
		return 1;
	strcpy (g_page, "written after the fault");		/* fault, caught */
	printf ("%s (%d fault)\n", g_page, (int) g_faults);
	munmap (g_page, (size_t) g_size);
	return 0;
}
```

## Example: modern C++

`std::filesystem`, `std::async` and exceptions work as anywhere else.

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
		std::printf ("%-5s %8ju KB\n", dirs[i].c_str (), parts[i].get () / 1024);
	try
	{
		auto n = fs::file_size ("/tmp/absent");
		std::printf ("%ju bytes?\n", n);
	}
	catch (const fs::filesystem_error &e)
	{
		std::printf ("expected exception: %s\n", e.what ());
	}
	return 0;
}
```

## Pitfalls

- **No `fork`**: Unix code that does `fork` then `exec` is rewritten with `posix_spawn`.
- **`system ("a | b")`** does not go through a shell: no pipes, no redirections. Start `sh -c "..."` if you need them, or `posix_spawn` with file actions.
- **`/tmp` is in memory**: fast, but lost at shutdown. What must stay goes on `SD0:`.
- **A single time zone, UTC**: `localtime` returns the same time as `gmtime`.
- **`connect` always waits**, even non-blocking: in a graphical app, use a separate thread.
- **A graphical program** is still an ordinary C program: the window comes from [UIKit](uikit_en.md), the libc does the rest.

## See also

- [An application](app_en.md) and [the skeleton](skeleton_en.md): building an app with `pibuild`.
- [NetKit](netkit_en.md): the network under the sockets.
- [Writing a kit](kit_en.md), [IPC](ipc_en.md), [a service](service_en.md).
