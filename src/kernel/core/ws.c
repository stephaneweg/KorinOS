/* Canal du serveur graphique, comme src/kernel/ws.asm du Pi (docs/ABI.md,
 * 0x50-0x5B) : le noyau ne fait que fournir des mécanismes ; fenêtres,
 * compositeur et bureau sont dans un process (Elegant).
 *   canal : le service nommé « ws » (SVF_ENDS : prévenu des fins de process) ;
 *   événements : une file de 64 x 32 octets par process, remplie par le
 *     serveur (et par un service SVF_POST : busd), lue par EV_WAIT ;
 *   écran : WS_DISPLAY donne le framebuffer au serveur (écriture combinée) ;
 *     tant qu'il l'a, la console du noyau se tait, et reprend s'il disparaît ;
 *   presse-papier : CLIP_SET / CLIP_GET, 64 Ko.
 * Portable : l'écran vient de l'architecture (arch_fb_info). */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/errno.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/svc.h>
#include <korin/vm.h>
#include <korin/ws.h>

#define EVQ 64
#define EV_SIZE 32
#define CLIP_MAX 65536
#define WS_FB_VA 0x6D00000000ULL /* le framebuffer chez le serveur */

struct evq {
	uint8_t ev[EVQ][EV_SIZE];
	int head, count;
};

static spinlock_t ws_lock = SPINLOCK_INIT;
static int ws_chan = -1;
static int ws_pid;
static int ev_key;
static char *clip;
static uint64_t clip_len;
static uint32_t bar_height;

int64_t sys_ws_register(void)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	int64_t ch = svc_register_name("ws", SVF_ENDS, p);
	if (ch < 0)
		return ch;
	uint64_t f = spin_lock_irqsave(&ws_lock);
	ws_chan = (int)ch;
	ws_pid = p->pid;
	spin_unlock_irqrestore(&ws_lock, f);
	return 0;
}

static int server_chan(void)
{
	uint64_t f = spin_lock_irqsave(&ws_lock);
	int c = ws_chan;
	spin_unlock_irqrestore(&ws_lock, f);
	return c;
}

int64_t sys_ws_call(uint64_t msg)
{
	int c = server_chan();
	return c < 0 ? E_NODEV : sys_svc_call((uint64_t)c, msg);
}

int64_t sys_ws_next(uint64_t msg, uint64_t ms)
{
	int c = server_chan();
	return c < 0 ? E_NODEV : sys_svc_next((uint64_t)c, msg, ms);
}

int64_t sys_ws_reply(uint64_t num, uint64_t msg)
{
	return sys_svc_reply(num, msg);
}

int64_t sys_ws_signal(uint64_t bits)
{
	int c = server_chan();
	if (c < 0)
		return E_NODEV;
	svc_signal_kernel(c, bits);
	return 0;
}

/* ---- événements */

int64_t sys_ws_post(int64_t pid, uint64_t uev)
{
	struct process *me = sched_current()->proc;
	if (me->pid != ws_pid && !svc_owner_has(me, SVF_POST))
		return E_PERM;
	uint8_t ev[EV_SIZE];
	if (copy_from_user(ev, uev, EV_SIZE))
		return E_FAULT;
	uint64_t f = sched_lock_take();
	struct process *p = proc_get((int)pid);
	struct evq *q = p ? p->evq : 0;
	if (p && !q)
		q = p->evq = kzalloc(sizeof *q); /* (le tas a son propre verrou) */
	int ok = p && q;
	if (ok) {
		if (q->count == EVQ) { /* pleine : la plus ancienne part */
			q->head = (q->head + 1) % EVQ;
			q->count--;
		}
		memcpy(q->ev[(q->head + q->count) % EVQ], ev, EV_SIZE);
		q->count++;
	}
	sched_lock_give(f);
	if (!ok)
		return p ? E_NOMEM : E_SRCH;
	sched_wake_all(&ev_key);
	return 0;
}

int64_t sys_ev_wait(uint64_t uev, uint64_t ms)
{
	struct process *p = sched_current()->proc;
	uint64_t deadline = arch_ticks_ms() + ms;
	for (;;) {
		uint8_t ev[EV_SIZE];
		int got = 0;
		uint64_t f = sched_lock_take();
		struct evq *q = p->evq;
		if (q && q->count) {
			memcpy(ev, q->ev[q->head], EV_SIZE);
			q->head = (q->head + 1) % EVQ;
			q->count--;
			got = 1;
		}
		sched_lock_give(f);
		if (got) {
			if (copy_to_user(uev, ev, EV_SIZE))
				return E_FAULT;
			uint32_t type;
			memcpy(&type, ev, 4);
			return type;
		}
		uint64_t now = arch_ticks_ms();
		if (ms == 0 || now >= deadline)
			return 0;
		sched_prepare_block(deadline - now, &ev_key);
		f = sched_lock_take();
		int ready = p->evq && ((struct evq *)p->evq)->count;
		sched_lock_give(f);
		if (ready)
			sched_wake(sched_current());
		int why = sched_block();
		if (why == WAKE_KILLED || why == WAKE_SIGNAL)
			return 0; /* EV_WAIT rend 0 sur un signal (comme le Pi) */
	}
}

/* ---- écran */

static int64_t fb_info_out(uint64_t out, uint64_t va)
{
	struct fb_desc d;
	arch_fb_info(&d);
	uint64_t o[5] = {va, d.width, d.height, d.pitch_bytes, d.phys};
	return copy_to_user(out, o, 40) ? E_FAULT : 0;
}

int64_t sys_ws_display(uint64_t out)
{
	struct process *p = sched_current()->proc;
	if (p->pid != ws_pid || !p->vm)
		return E_PERM;
	struct fb_desc d;
	arch_fb_info(&d);
	uint64_t size = ALIGN_UP(d.pitch_bytes * d.height, PAGE_SIZE);
	for (uint64_t off = 0; off < size; off += PAGE_SIZE)
		if (vm_map_page(p->vm, WS_FB_VA + off, d.phys + off, VM_W | VM_SHARED | VM_WC))
			return E_NOMEM;
	console_silence(1); /* l'écran est au serveur */
	return fb_info_out(out, WS_FB_VA);
}

int64_t sys_ws_mode(uint64_t w, uint64_t h, uint64_t out)
{
	struct process *p = sched_current()->proc;
	if (p->pid != ws_pid)
		return E_PERM;
	if (w < 640 || h < 480 || w > 2560 || h > 1600 || (w & 1))
		return E_INVAL;
	/* le mode GOP est fixé par l'UEFI : la résolution actuelle reste (comme un
	 * refus du firmware du Pi) */
	int64_t r = fb_info_out(out, WS_FB_VA);
	return r ? r : E_IO;
}

int64_t sys_screen_info(uint64_t out)
{
	struct fb_desc d;
	arch_fb_info(&d);
	uint64_t o[3] = {d.width, d.height, bar_height};
	return copy_to_user(out, o, 24) ? E_FAULT : 0;
}

/* ---- presse-papier */

int64_t sys_clip_set(uint64_t buf, uint64_t len)
{
	if (len > CLIP_MAX)
		return E_INVAL;
	char *c = len ? kmalloc(len) : 0;
	if (len && !c)
		return E_NOMEM;
	if (len && copy_from_user(c, buf, len)) {
		kfree(c);
		return E_FAULT;
	}
	uint64_t f = spin_lock_irqsave(&ws_lock);
	char *old = clip;
	clip = c;
	clip_len = len;
	spin_unlock_irqrestore(&ws_lock, f);
	kfree(old);
	return 0;
}

int64_t sys_clip_get(uint64_t buf, uint64_t max)
{
	char *tmp = kmalloc(CLIP_MAX);
	if (!tmp)
		return E_NOMEM;
	uint64_t f = spin_lock_irqsave(&ws_lock);
	uint64_t n = clip_len;
	if (n)
		memcpy(tmp, clip, n);
	spin_unlock_irqrestore(&ws_lock, f);
	if (n > max)
		n = max;
	int64_t r = n && copy_to_user(buf, tmp, n) ? E_FAULT : (int64_t)clip_len;
	kfree(tmp);
	return r;
}

/* fin d'un process : sa file ; le serveur parti, la console reprend l'écran */
void ws_proc_exit(struct process *p)
{
	kfree(p->evq);
	p->evq = 0;
	uint64_t f = spin_lock_irqsave(&ws_lock);
	int was = p->pid == ws_pid;
	if (was) {
		ws_pid = 0;
		ws_chan = -1;
	}
	spin_unlock_irqrestore(&ws_lock, f);
	if (was)
		console_silence(0);
}
