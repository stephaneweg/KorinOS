# The IPC Functions

Korin OS programs talk to each other in three ways:

| Means | For | From C |
|---|---|---|
| **Named services** | a server program answering requests: `vfs`, `net`, `sound`, `print`... | calls `0x30`–`0x38` ([Writing a Service](service_en.md)) |
| **Channels** | a stream or packets between two processes, with descriptor passing | `socketpair`, `send`, `recv`, `sendmsg` (`SCM_RIGHTS`), `poll` |
| **Shared memory** | large volumes without copying (images, sound, documents) | `shm_open`, `memfd_create`, `ftruncate`, `mmap (MAP_SHARED)` |

Add to these **pipes** (`pipe`, the output of a program started by `posix_spawn`) and **futexes** (`pthread_mutex`, `pthread_cond`, `sem_t` between threads of one process). Channels and shared memory are the kernel calls `0x70`–`0x7D`; the POSIX layer serves them under their usual names, which is the recommended path.

## Channels

`socketpair (AF_UNIX, type, 0, fd)` creates two connected ends: what is written into one is read from the other.

| Type | Behaviour |
|---|---|
| `SOCK_STREAM` | a byte stream, like a two-way pipe |
| `SOCK_SEQPACKET` | messages kept whole, in order (the handiest for a protocol) |
| `SOCK_DGRAM` | messages, without connection |

Each end has a 64 KB receive ring; a bigger message is refused (`EMSGSIZE`). `sendmsg` can carry descriptors (`SCM_RIGHTS`): another channel, a shared memory. An end is given to a program started by `posix_spawn` (`posix_spawn_file_actions_adddup2`): this is how Jet talks to its WebKit processes.

Example: a worker thread receives commands through a packet channel and answers through the same channel; the main thread waits for the answer with `poll`.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>

static int g_fd[2];

// the worker: every packet received is a command, it answers with a packet
static void *worker (void *arg)
{
	(void) arg;
	char buf[256];
	for (;;)
	{
		ssize_t n = recv (g_fd[1], buf, sizeof buf - 1, 0);
		if (n <= 0)
			break;				// 0: the other end is closed
		buf[n] = 0;
		char out[300];
		int k = snprintf (out, sizeof out, "received \"%s\" (%d bytes)", buf, (int) n);
		send (g_fd[1], out, (size_t) k, 0);
	}
	return 0;
}

int main (void)
{
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, g_fd) < 0)
	{
		perror ("socketpair");
		return 1;
	}
	pthread_t th;
	pthread_create (&th, 0, worker, 0);
	const char *cmds[] = { "hello", "open document.txt", "end" };
	for (int i = 0; i < 3; i++)
	{
		send (g_fd[0], cmds[i], strlen (cmds[i]), 0);
		struct pollfd p = { g_fd[0], POLLIN, 0 };
		if (poll (&p, 1, 2000) <= 0)		// 2 s at most
		{
			printf ("no answer\n");
			break;
		}
		char rep[300];
		ssize_t n = recv (g_fd[0], rep, sizeof rep - 1, 0);
		rep[n > 0 ? n : 0] = 0;
		printf ("%s\n", rep);
	}
	close (g_fd[0]);				// the worker sees the end and stops
	pthread_join (th, 0);
	return 0;
}
```

## Shared Memory

A shared memory object has pages of its own, mapped into every process that opens it: what one writes, the other sees at once. `shm_open` names it (`/name`, 55 bytes at most, 256 names in the system); `memfd_create` creates it without a name, to pass it through a channel. It lives as long as a process holds it (descriptor or mapping) or its name exists: `shm_unlink` removes the name.

Example: the same program writes (`share write text`) then reads (`share read`) a shared board; start both from two Terminals.

```c
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

struct board				// what both processes see
{
	unsigned version;
	char text[1020];
};

int main (int argc, char **argv)
{
	if (argc < 2)
	{
		printf ("share write <text> | read | remove\n");
		return 1;
	}
	if (!strcmp (argv[1], "remove"))
		return shm_unlink ("/korin-board") < 0;
	int fd = shm_open ("/korin-board", O_RDWR | O_CREAT, 0600);
	if (fd < 0)
	{
		perror ("shm_open");
		return 1;
	}
	ftruncate (fd, sizeof (struct board));	// (no effect if it already has its size)
	struct board *b = (struct board *) mmap (0, sizeof (struct board), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close (fd);				// the mapping keeps the object
	if (b == MAP_FAILED)
	{
		perror ("mmap");
		return 1;
	}
	if (!strcmp (argv[1], "write") && argc > 2)
	{
		snprintf (b->text, sizeof b->text, "%s", argv[2]);
		__atomic_add_fetch (&b->version, 1, __ATOMIC_RELEASE);
		printf ("written, version %u\n", b->version);
	}
	else
		printf ("version %u: %s\n", __atomic_load_n (&b->version, __ATOMIC_ACQUIRE), b->text);
	munmap (b, sizeof (struct board));
	return 0;
}
```

> **Note**: shared memory synchronises nothing. To wait for the other process, send it a word through a channel, or a `SVC_SIGNAL` if it is a service.

## The Kernel Calls

What the POSIX layer uses, for whoever writes assembly or needs a detail POSIX hides. A **handle** (1 to 1023) designates a channel end or a shared object in the process table.

| No. | Call | Arguments | Result |
|---|---|---|---|
| `0x70` | `HANDLE_CLOSE` | handle | 0 |
| `0x71` | `HANDLE_INFO` | handle, 64-byte output | 0: kind, type, pending bytes or size, state, creator pid |
| `0x72` | `HANDLE_WAIT` | entries {handle 32 bits, events 16, ready 16}, count (64 at most), wait ms (< 0: forever) | number of ready entries (`POLLIN` 1, `POLLOUT` 4, `POLLERR` 8, `POLLHUP` 16) |
| `0x73` | `HANDLE_LIST` | output, room, 1: marks cleared | the handles received from the parent |
| `0x74` | `HANDLE_INHERIT` | entries {handle, mark}, count | 0: the next process started receives them |
| `0x75` | `CHAN_CREATE` | `1` stream / `2` datagrams / `5` packets, output 2 × 32 bits | 0: both ends |
| `0x76` | `CHAN_SEND` | handle, message (iovecs and carried handles, 32 bytes), `1` no wait, wait ms | bytes sent |
| `0x77` | `CHAN_RECV` | handle, message, `1` no wait \| `2` peek, wait ms | bytes (0: the end) |
| `0x78` | `CHAN_SHUTDOWN` | handle, `1` read \| `2` write | 0 |
| `0x79` | `SHM_CREATE` | size, `1` sealable | handle (`memfd_create`) |
| `0x7A` | `SHM_OPEN` | name, `1` create \| `2` exclusive \| `4` truncate \| `8` weak name | handle |
| `0x7B` | `SHM_UNLINK` | name | 0 |
| `0x7C` | `SHM_CTL` | handle, `1` size / `2` seals / `3` pin / `4` unpin, value | 0 |
| `0x7D` | `SHM_MAP` | handle, address, size, rights, flags, offset | address |
| `0x20`–`0x2F` | `FUTEX_WAIT`, `FUTEX_WAKE`... | address, value, wait | waiting on a memory word (pthread) |

The details (limits, locks, what happens when a process ends) are in `docs/ABI.md`, section `0x70`.

## Choosing

- **A single server** that several programs query (a driver, a system setting): a [service](service_en.md).
- **Two linked programs** (an app and its helper process, a player and its decoder): a channel created by the parent, given to the child by `posix_spawn`.
- **Large buffers** (an image, samples): a shared memory, announced by a message on a channel or by a request to the service.
- **Threads of one program**: `pthread_mutex`, `pthread_cond`, `sem_t`, and the process memory itself.

## See Also

- [Writing a Service](service_en.md)
- [The C Library and the POSIX Layer](libc_en.md)
