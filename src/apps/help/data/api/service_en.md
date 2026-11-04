# Writing a Service

A **service** is a program that registers under a name with the kernel (`vfs`, `net`, `sound`, `print`...) and answers the requests of other programs. The client opens the service by its name, sends a 512-byte message and receives the answer in the same message: this is the basic IPC of Korin OS, synchronous, with no copy beyond the message.

## The Message

| Offset | Field | Role |
|---|---|---|
| +0 | `op` | the request; in the answer: the result (also returned by `SVC_CALL`) |
| +8, +16, +24, +32 | `a`, `b`, `c`, `d` | four 64-bit arguments |
| +40 | `len` (32 bits) | useful bytes of `data` |
| +44 | `pid` (32 bits) | the calling process, filled by the kernel |
| +48 | `data` | 464 bytes of data |

For more than 464 bytes: shared memory ([IPC](ipc_en.md#shared-memory)), or several requests.

## The Calls

| No. | Call | Arguments | Result |
|---|---|---|---|
| `0x30` | `SVC_REGISTER` | name (31 bytes at most), flags | service number; only for a program of `SD0:/sys`; an error if the name is taken |
| `0x31` | `SVC_OPEN` | name, wait in ms | channel; `E_NOENT` (-2) if the service is not there at the end of the wait |
| `0x32` | `SVC_CALL` | channel, message | the result (`op` of the answer); `E_PIPE` (-32) if the service stops |
| `0x33` | `SVC_NEXT` | service number, message, wait in ms | request number (0: nothing during the wait) |
| `0x34` | `SVC_REPLY` | request number, message | 0 |
| `0x35` | `SVC_SIGNAL` | channel, bits | 0: wakes the service without waiting; it receives op `0xFFFD`, `a` = the bits |

Flags of `SVC_REGISTER`: 1 (`SVF_FINS`): the service is told about the end of every process (op `0xFFFF`, `a` = pid, `b` = exit code), useful to release what a client held; 2: told about births (op `0xFFFE`); 4: it keeps memory it can give back. These kernel notices expect no answer. The other calls (`0x36` to `0x38`: memory of the caller, for drivers) are described in `docs/ABI.md`.

All these calls go through `svc 0`, the number in `x8`: see `sys3` in the examples. The C library has no function for them.

## A Service: counter

The "compteur" (counter) service keeps a total: `CT_ADD` adds `a` to it, `CT_GET` returns it, `CT_HELLO` answers a text in `data`.

```c
// compteur -- a sample service: registers as "compteur" and keeps a total
#include <stdio.h>
#include <string.h>

struct svc_msg					// the 512-byte message
{
	long op, a, b, c, d;
	unsigned len, pid;
	char data[464];
};
enum { SYS_SVC_REGISTER = 0x30, SYS_SVC_NEXT = 0x33, SYS_SVC_REPLY = 0x34 };
enum { CT_ADD = 1, CT_GET = 2, CT_HELLO = 3 };	// the requests of the service
enum { SVF_FINS = 1, OP_NOTICES = 0xFFF0 };	// (op >= 0xFFF0: a kernel notice, no answer)

static long sys3 (long n, long a, long b, long c)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory", "cc");
	return x0;
}

int main (void)
{
	long svc = sys3 (SYS_SVC_REGISTER, (long) "compteur", SVF_FINS, 0);
	if (svc < 0)
	{
		printf ("compteur: registration refused (%ld): already running, or not in SD0:/sys\n", svc);
		return 1;
	}
	static struct svc_msg m;
	long total = 0;
	for (;;)
	{
		long rq = sys3 (SYS_SVC_NEXT, svc, (long) &m, 60000);
		if (rq < 0)
			return 1;
		if (rq == 0 || m.op >= OP_NOTICES)		// nothing, or a notice (end of a process...)
			continue;
		long r;
		switch (m.op)
		{
		case CT_ADD: total += m.a; r = total; break;
		case CT_GET: r = total; break;
		case CT_HELLO:
			m.len = (unsigned) snprintf (m.data, sizeof m.data, "Hello, process %u: the total is %ld.", m.pid, total);
			r = 0;
			break;
		default: r = -22;				// EINVAL
		}
		m.op = r;
		sys3 (SYS_SVC_REPLY, rq, (long) &m, 0);
	}
}
```

## A Client

Any program can open a service, without special rights.

```c
// ajoute -- sends a number to the "compteur" service
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct svc_msg
{
	long op, a, b, c, d;
	unsigned len, pid;
	char data[464];
};
enum { SYS_SVC_OPEN = 0x31, SYS_SVC_CALL = 0x32 };
enum { CT_ADD = 1, CT_GET = 2, CT_HELLO = 3 };

static long sys3 (long n, long a, long b, long c)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory", "cc");
	return x0;
}

int main (int argc, char **argv)
{
	long ch = sys3 (SYS_SVC_OPEN, (long) "compteur", 2000, 0);	// waits 2 s at most for the service
	if (ch < 0)
	{
		printf ("the compteur service is not running (%ld)\n", ch);
		return 1;
	}
	static struct svc_msg m;
	memset (&m, 0, sizeof m);
	m.op = CT_ADD;
	m.a = argc > 1 ? atol (argv[1]) : 1;
	long total = sys3 (SYS_SVC_CALL, ch, (long) &m, 0);
	printf ("total: %ld\n", total);
	memset (&m, 0, sizeof m);
	m.op = CT_HELLO;
	if (sys3 (SYS_SVC_CALL, ch, (long) &m, 0) >= 0)
		printf ("%.*s\n", (int) m.len, m.data);
	return 0;
}
```

## Building, Starting

A service can only register from `SD0:/sys`: it is built with the `sys` or `svc` type.

```sh
pibuild svc SD0:/home/projects/compteur compteur
pibuild bin SD0:/home/projects/ajoute ajoute
SD0:/sys/svc/compteur
ajoute 5
```

| To | Do |
|---|---|
| try it | start it from the Terminal: a program of `SD0:/sys` goes through `init2`, which gives it the rights; its output goes to the log, not to the Terminal |
| start it at every boot | add `SD0:/sys/svc/compteur` to `SD0:/etc/services.txt`: `init2` starts it before the apps and restarts it if it falls |
| start it with the desktop | add its line to `SD0:/etc/session` (programs started by Elegant) |
| start it on demand | the client opens the service; if it is not there, it asks `init2` to start it (`INIT_OP_LAUNCH`), then opens again with a wait |

## Advice

- **Keep requests short**: the client waits for the answer (30 s at most). A long job runs in a thread of the service; the request returns a job number at once, which the client polls afterwards (this is what the print service does, `src/sys/printd`).
- **`SVF_FINS`**: when a client ends, release what it held (locks, files opened for it).
- **Stable op numbers**: like kits, a service keeps its ops; it adds some, it never changes their meaning.
- **A shared header** between the service and its clients (`compteur.h`: the ops, the structure), rather than copied numbers.
- A service may also answer later: keep the request number and call `SVC_REPLY` afterwards, from another thread.

## See Also

- [The IPC Functions](ipc_en.md): channels, shared memory, waiting on several handles.
- [Writing a Kit](kit_en.md): a library that hides the service from its clients (as PrinterKit does in front of `print`).
