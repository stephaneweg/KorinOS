/* Appels système : numéros de src/inc/syscalls.inc (korin/syscall_nr.h en
 * est tiré à la construction), mêmes arguments et résultats que le Pi
 * (docs/ABI.md). Ce qui n'est pas encore fait rend E_NOSYS. Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/boot.h>
#include <korin/hw.h>
#include <korin/image.h>
#include <korin/ipc.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/proc.h>
#include <korin/sched.h>
#include <korin/signal.h>
#include <korin/svc.h>
#include <korin/syscall.h>
#include <korin/syscall_nr.h>
#include <korin/vm.h>
#include <korin/ws.h>

#define PRINT_MAX 4096
#define MB_QUERY 0
#define MB_PRIVATE 8
#define MB_REGISTER 16
#define MB_PRIVATE_SYNC 32
#define MB_REGISTER_SYNC 64

static int64_t sys_print(uint64_t s, uint64_t len)
{
	if (len > PRINT_MAX)
		len = PRINT_MAX;
	char buf[257];
	for (uint64_t done = 0; done < len;) {
		uint64_t n = len - done > 256 ? 256 : len - done;
		if (copy_from_user(buf, s + done, n))
			return E_FAULT;
		buf[n] = 0;
		kprintf("%s", buf);
		done += n;
	}
	return 0;
}

static int64_t sys_membarrier(uint64_t cmd)
{
	switch (cmd) {
	case MB_QUERY:
		return MB_PRIVATE | MB_REGISTER | MB_PRIVATE_SYNC | MB_REGISTER_SYNC;
	case MB_PRIVATE:
	case MB_PRIVATE_SYNC: {
		/* une interruption sur chaque autre cœur : chacun passe une barrière */
		struct vm_space *vm = sched_current()->proc->vm;
		if (vm)
			vm_tlb_shootdown(vm);
		return 0;
	}
	case MB_REGISTER:
	case MB_REGISTER_SYNC:
		return 0;
	}
	return E_INVAL;
}

static int64_t sys_proc_stats(int64_t pid, uint64_t buf, uint64_t places)
{
	if (places < 1)
		return E_INVAL;
	uint64_t w[129];
	uint64_t n = places < 129 ? places : 129;
	uint64_t f = sched_lock_take();
	struct process *p = pid ? proc_get((int)pid) : sched_current()->proc;
	if (p) {
		w[0] = p->svc_n;
		for (uint64_t i = 1; i < n; i++)
			w[i] = p->svc_tab[i - 1];
	}
	sched_lock_give(f);
	if (!p)
		return E_SRCH;
	return copy_to_user(buf, w, n * 8) ? E_FAULT : (int64_t)n;
}

static int64_t dispatch(uint64_t nr, const uint64_t a[6])
{
	struct process *p = sched_current()->proc;
	p->svc_n++; /* sans verrou, comme le Pi : deux threads peuvent perdre un compte */
	if (nr < 128)
		p->svc_tab[nr]++;
	switch (nr) {
	/* 0x00 mémoire */
	case SYS_SBRK: return sys_sbrk((int64_t)a[0]);
	case SYS_BUF_CREATE: return sys_buf_create(a[0]);
	case SYS_BUF_GRANT: return sys_buf_grant(a[0], (int64_t)a[1]);
	case SYS_BUF_MAP: return sys_buf_map(a[0]);
	case SYS_BUF_FREE: return sys_buf_free(a[0]);
	case SYS_DMA_ALLOC: return sys_dma_alloc(a[0], a[1]);
	case SYS_VM_MAP: return sys_vm_map(a[0], a[1], a[2], a[3]);
	case SYS_VM_UNMAP: return sys_vm_unmap(a[0], a[1]);
	case SYS_VM_PROTECT: return sys_vm_protect(a[0], a[1], a[2]);
	case SYS_VM_ADVISE: return sys_vm_advise(a[0], a[1], a[2]);
	case SYS_VM_QUERY: return sys_vm_query(a[0], a[1]);
	case SYS_VM_STATS: return sys_vm_stats((int64_t)a[0], a[1]);
	case SYS_VM_PAGEINFO: return sys_vm_pageinfo(a[0]);
	case SYS_BUF_INFO: return sys_buf_info(a[0], a[1]);
	case SYS_TLS_SET:
		if (a[0] >= USER_STACK_TOP) /* (adresse du programme, canonique) */
			return E_INVAL;
		sched_current()->user_tls = a[0];
		arch_set_tls(a[0]);
		return 0;
	/* 0x10 process et temps */
	case SYS_EXIT: proc_exit((int)a[0]);
	case SYS_GETPID: return p->pid;
	case SYS_SPAWN_IMAGE: return sys_spawn_image(a[0], a[1], a[2]);
	case SYS_WAIT: return sys_wait((int64_t)a[0], a[1], a[2]);
	case SYS_KILL: return sys_kill((int64_t)a[0]);
	case SYS_PROCLIST: return sys_proclist_from(a[0], a[1], 0);
	case SYS_PROC_FLAGS: return (int64_t)(p->flags | (p->rights ? 0x100 : 0));
	case SYS_SLEEP: return sched_sleep(a[0]);
	case SYS_YIELD: sched_yield(); return 0;
	case SYS_TIME_MS: return (int64_t)arch_ticks_ms();
	case SYS_CLOCK_GET: return sys_clock(0, a[0], 0, 0);
	case SYS_CLOCK_SET: return sys_clock(1, a[0], a[1], a[2]);
	case SYS_SPAWN_PRELOADED: return sys_spawn_preloaded(a[0], a[1], a[2]);
	case SYS_PROC_RIGHTS: return sys_proc_rights((int64_t)a[0]);
	/* 0x90 identité (docs/etude/MULTI-UTILISATEUR.md §2.2) */
	case SYS_GETUID: return p->uid;
	case SYS_SETUID: return sys_setuid((int64_t)a[0]);
	case SYS_PROC_UID: return sys_proc_uid((int64_t)a[0]);
	case SYS_PROC_INFO: return sys_proc_info((int64_t)a[0], a[1]);
	case SYS_PROCLIST_FROM: return sys_proclist_from(a[0], a[1], a[2]);
	/* 0x20 threads et futex */
	case SYS_THREAD_CREATE: return sys_thread_create(a[0], a[1], a[2], 0, 0, 0);
	case SYS_THREAD_EXIT: thread_exit((int)a[0]);
	case SYS_FUTEX_WAIT: return sys_futex_wait(a[0], a[1], a[2]);
	case SYS_FUTEX_WAKE: return sys_futex_wake(a[0], a[1]);
	case SYS_THREAD_CREATE_EX: return sys_thread_create(a[0], a[1], a[2], a[3], a[4], a[5]);
	case SYS_THREAD_JOIN: return sys_thread_join((int64_t)a[0]);
	case SYS_THREAD_SELF: return sched_current()->tid;
	case SYS_THREAD_SET_PRIORITY: return sys_thread_set_priority((int64_t)a[0], (int64_t)a[1]);
	case SYS_THREAD_SET_NAME: return sys_thread_set_name((int64_t)a[0], a[1]);
	case SYS_THREAD_KILL: return sys_thread_kill((int64_t)a[0], (int64_t)a[1]);
	case SYS_THREAD_LIST: return sys_thread_list((int64_t)a[0], a[1], a[2]);
	case SYS_SIG_ACTION: return sys_sig_action(a[0], a[1], a[2], a[3], a[4]);
	case SYS_SIG_SEND: return sys_sig_send((int64_t)a[0], (int64_t)a[1], a[2]);
	case SYS_SIG_RETURN: return sys_sig_return();
	case SYS_SIG_MASK: return sys_sig_mask(a[0], a[1]);
	case SYS_SIG_ALTSTACK: return sys_sig_altstack(a[0], a[1], a[2]);
	/* 0x70 IPC entre process */
	case SYS_HANDLE_CLOSE: return sys_handle_close(a[0]);
	case SYS_HANDLE_INFO: return sys_handle_info(a[0], a[1]);
	case SYS_HANDLE_WAIT: return sys_handle_wait(a[0], a[1], (int64_t)a[2]);
	case SYS_HANDLE_LIST: return sys_handle_list(a[0], a[1], a[2]);
	case SYS_HANDLE_INHERIT: return sys_handle_inherit(a[0], a[1]);
	case SYS_CHAN_CREATE: return sys_chan_create(a[0], a[1]);
	case SYS_CHAN_SEND: return sys_chan_send(a[0], a[1], a[2], a[3]);
	case SYS_CHAN_RECV: return sys_chan_recv(a[0], a[1], a[2], a[3]);
	case SYS_CHAN_SHUTDOWN: return sys_chan_shutdown(a[0], a[1]);
	case SYS_SHM_CREATE: return sys_shm_create(a[0], a[1]);
	case SYS_SHM_OPEN: return sys_shm_open(a[0], a[1]);
	case SYS_SHM_UNLINK: return sys_shm_unlink(a[0]);
	case SYS_SHM_CTL: return sys_shm_ctl(a[0], a[1], a[2]);
	case SYS_SHM_MAP: return sys_shm_map(a[0], a[1], a[2], a[3], a[4], a[5]);
	/* 0x30 services nommés */
	case SYS_SVC_REGISTER: return sys_svc_register(a[0], a[1]);
	case SYS_SVC_OPEN: return sys_svc_open(a[0], a[1]);
	case SYS_SVC_CALL: return sys_svc_call(a[0], a[1]);
	case SYS_SVC_NEXT: return sys_svc_next(a[0], a[1], a[2]);
	case SYS_SVC_REPLY: return sys_svc_reply(a[0], a[1]);
	case SYS_SVC_SIGNAL: return sys_svc_signal(a[0], a[1]);
	case SYS_SVC_FORWARD: return sys_svc_forward(a[0], a[1], a[2]);
	case SYS_SVC_MAP_CALLER: return sys_svc_map_caller(a[0], a[1], a[2], a[3]);
	case SYS_SVC_UNMAP_CALLER: return sys_svc_unmap_caller(a[0]);
	case SYS_SVC_CALLER_PHYS: return sys_svc_caller_phys(a[0], a[1], a[2]);
	/* 0x40 matériel */
	case SYS_DEV_MAP: return sys_dev_map(a[0], a[1]);
	case SYS_MOD_LAUNCH: return sys_mod_lancer(a[0], a[1]);
	case SYS_MOD_READ: return sys_mod_lire(a[0], a[1], a[2]);
	case SYS_BOOT_READY: return sys_demarrage_pret();
	case SYS_BOOT_FILE: return sys_boot_file(a[0], a[1], a[2]);
	case SYS_KIT_REGISTER: return sys_kit_register(a[0], a[1], a[2]);
	case SYS_IMAGE_PRELOAD: return sys_image_preload(a[0], a[1], a[2]);
	case SYS_IMAGE_FORGET: return sys_image_forget(a[0]);
	case SYS_IRQ_BIND: return sys_irq_bind(a[0], (int64_t)a[1], a[2]);
	case SYS_IRQ_ACK: return sys_irq_ack(a[0]);
	case SYS_IRQ_WAIT: return sys_irq_wait(a[0], a[1]);
	case SYS_IRQ_UNBIND: return sys_irq_unbind(a[0]);
	case SYS_ACPI_TABLE: return sys_acpi_table(a[0], a[1], a[2], a[3]);
	case SYS_IRQ_MSI: return sys_irq_msi(a[0], a[1]);
	case SYS_IO_PORT: return sys_io_port(a[0], a[1], a[2]);
	case SYS_POWER: return sys_power(a[0]);
	/* 0x50 serveur graphique */
	case SYS_WS_REGISTER: return sys_ws_register();
	case SYS_WS_CALL: return sys_ws_call(a[0]);
	case SYS_WS_NEXT: return sys_ws_next(a[0], a[1]);
	case SYS_WS_REPLY: return sys_ws_reply(a[0], a[1]);
	case SYS_WS_POST: return sys_ws_post((int64_t)a[0], a[1]);
	case SYS_WS_DISPLAY: return sys_ws_display(a[0]);
	case SYS_EV_WAIT: return sys_ev_wait(a[0], a[1]);
	case SYS_SCREEN_INFO: return sys_screen_info(a[0]);
	case SYS_CLIP_SET: return sys_clip_set(a[0], a[1]);
	case SYS_CLIP_GET: return sys_clip_get(a[0], a[1]);
	case SYS_WS_SIGNAL: return sys_ws_signal(a[0]);
	case SYS_WS_MODE: return sys_ws_mode(a[0], a[1], a[2]);
	/* 0x60 système */
	case SYS_PRINT: return sys_print(a[0], a[1]);
	case SYS_KLOG: return sys_klog(a[0], a[1]);
	case SYS_SYSINFO: return sys_sysinfo(a[0]);
	case SYS_CPU_LOAD: return sys_cpu_load(a[0], a[1]);
	case SYS_GETRANDOM: return sys_getrandom(a[0], a[1]);
	case SYS_LIB_OPEN: return sys_lib_open(a[0], a[1]);
	case SYS_IMAGE_LIST: return sys_image_list(a[0], a[1]);
	case SYS_IMAGE_CREATE: return sys_image_create(a[0], a[1], a[2]);
	case SYS_IMAGE_SEAL: return sys_image_seal(a[0], a[1]);
	case SYS_MEMBARRIER: return sys_membarrier(a[0]);
	case SYS_PROC_STATS: return sys_proc_stats((int64_t)a[0], a[1], a[2]);
	case SYS_GETPPID: return sys_getppid();
	case SYS_SETPGID: return sys_setpgid((int64_t)a[0], (int64_t)a[1]);
	case SYS_GETPGID: return sys_getpgid((int64_t)a[0]);
	case SYS_SETITIMER: return sys_setitimer(a[0], (int64_t)a[1], (int64_t)a[2], a[3]);
	case SYS_FORK: return sys_fork();
	case SYS_EXEC: return sys_exec(a[0], a[1], a[2]);
	}
	return E_NOSYS;
}

int64_t syscall_dispatch(uint64_t nr, const uint64_t a[6])
{
	return dispatch(nr, a);
}
