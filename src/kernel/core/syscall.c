/* Appels système : numéros de src/inc/syscalls.inc (korin/syscall_nr.h en
 * est tiré à la construction). Premiers appels de base ; les autres
 * rendent E_NOSYS. Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/lib.h>
#include <korin/sched.h>
#include <korin/syscall.h>
#include <korin/syscall_nr.h>
#include <korin/vm.h>

#define PRINT_MAX 4096

/* la plage est-elle dans la moitié du process et présente ? */
static int user_range(uint64_t addr, uint64_t len)
{
	if (addr + len < addr || addr + len > vm_user_top)
		return 0;
	struct vm_space *vm = sched_current()->proc->vm;
	if (!vm)
		return 0;
	for (uint64_t a = ALIGN_DOWN(addr, 4096); a < addr + len; a += 4096)
		if (!vm_translate(vm, a))
			return 0;
	return 1;
}

static int64_t sys_print(uint64_t s, uint64_t len)
{
	if (len > PRINT_MAX)
		len = PRINT_MAX;
	if (!user_range(s, len))
		return E_FAULT;
	char buf[257];
	for (uint64_t done = 0; done < len;) {
		uint64_t n = len - done > 256 ? 256 : len - done;
		memcpy(buf, (const char *)s + done, n);
		buf[n] = 0;
		kprintf("%s", buf);
		done += n;
	}
	return (int64_t)len;
}

int64_t syscall_dispatch(uint64_t nr, const uint64_t a[6])
{
	switch (nr) {
	case SYS_PRINT:
		return sys_print(a[0], a[1]);
	case SYS_EXIT:
		proc_exit((int)a[0]);
	case SYS_THREAD_EXIT:
		thread_exit((int)a[0]);
	case SYS_GETPID:
		return sched_current()->proc->pid;
	case SYS_THREAD_SELF:
		return sched_current()->tid;
	case SYS_SLEEP:
		sched_sleep(a[0]);
		return 0;
	case SYS_YIELD:
		sched_yield();
		return 0;
	case SYS_TIME_MS:
		return (int64_t)arch_ticks_ms();
	}
	return E_NOSYS;
}
