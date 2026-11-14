/* Trame d'un gestionnaire de signal sur x86-64, et entrée C des appels
 * système. La trame suit celle de Linux x86-64 (comme le Pi suit Linux
 * arm64) : sur la pile du programme, de bas en haut :
 *   +0    adresse de retour = celle donnée à SIG_ACTION (elle fait SIG_RETURN)
 *   +8    siginfo (128 o) : +0 signo, +8 code, +16 adresse
 *   +136  ucontext : +0 flags, +8 link, +16 ss_sp, +24 ss_flags,
 *         +32 ss_size, +40 gregs[23] (ordre de Linux : r8..r15, rdi, rsi,
 *         rbp, rbx, rdx, rax, rcx, rsp, rip, eflags, csgsfs, err, trapno,
 *         oldmask, cr2), +224 pointeur vers l'état FPU, +296 masque,
 *         +320 état FPU, aligné sur 64 : FXSAVE (512 o), ou XSAVE (fpu_size
 *         octets) quand le noyau sauve l'AVX ; alors, comme Linux, les octets
 *         464..511 de la zone FXSAVE disent FP_XSTATE_MAGIC1, la taille et
 *         XCR0, et FP_XSTATE_MAGIC2 suit la zone
 * Le gestionnaire reçoit rdi = signal, rsi = siginfo, rdx = ucontext ; à son
 * retour, rsp pointe sur siginfo : SIG_RETURN relit l'ucontext à rsp + 128.
 * Trame faite dans un tampon du tas, pas par cœur : l'appel peut être
 * préempté (et repris sur un autre cœur) pendant la copie. */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/sched.h>
#include <korin/signal.h>
#include <korin/syscall.h>

#include "cpu.h"

#define SI_SIZE 128
#define UC_GREGS 40
#define UC_FPPTR 224
#define UC_MASK 296
#define UC_FP 320
#define FP_SW 464 /* struct _fpx_sw_bytes dans la zone FXSAVE */
#define FP_XSTATE_MAGIC1 0x46505853U
#define FP_XSTATE_MAGIC2 0x46505845U

enum { G_R8, G_R9, G_R10, G_R11, G_R12, G_R13, G_R14, G_R15, G_RDI, G_RSI, G_RBP, G_RBX, G_RDX, G_RAX,
       G_RCX, G_RSP, G_RIP, G_EFL, G_CSGSFS, G_ERR, G_TRAPNO, G_OLDMASK, G_CR2 };

uint64_t arch_frame_sp(void *frame)
{
	return ((struct regs *)frame)->rsp;
}

/* octets de l'état FPU dans la trame (avec FP_XSTATE_MAGIC2) */
static uint64_t fp_bytes(void)
{
	return fpu_xcr0 ? fpu_size + 4 : 512;
}

int arch_signal_frame(void *frame, int sig, int code, uint64_t addr, uint64_t handler, uint64_t restorer,
		      uint64_t old_mask, uint64_t stack_top)
{
	struct regs *r = frame;
	uint64_t base = stack_top ? stack_top : r->rsp - 128; /* zone rouge de System V gardée */
	uint64_t fpb = fp_bytes();
	uint64_t fp_user = (base - fpb) & ~63ULL;     /* état FPU aligné sur 64 */
	uint64_t sp = fp_user - (8 + SI_SIZE + UC_FP); /* (+8 : siginfo aligné sur 64) */
	uint64_t size = fp_user + fpb - sp;
	void *fmem;
	uint8_t *fp = fpu_area_alloc(&fmem); /* XSAVE : zone alignée sur 64 */
	uint8_t *b = kzalloc(size);
	if (!fp || !b) {
		kfree(fmem);
		kfree(b);
		return -1;
	}
	*(uint64_t *)b = restorer;
	uint8_t *si = b + 8, *uc = si + SI_SIZE;
	*(int32_t *)si = sig;
	*(int32_t *)(si + 8) = code;
	*(uint64_t *)(si + 16) = addr;
	uint64_t *g = (uint64_t *)(uc + UC_GREGS);
	g[G_R8] = r->r8, g[G_R9] = r->r9, g[G_R10] = r->r10, g[G_R11] = r->r11;
	g[G_R12] = r->r12, g[G_R13] = r->r13, g[G_R14] = r->r14, g[G_R15] = r->r15;
	g[G_RDI] = r->rdi, g[G_RSI] = r->rsi, g[G_RBP] = r->rbp, g[G_RBX] = r->rbx;
	g[G_RDX] = r->rdx, g[G_RAX] = r->rax, g[G_RCX] = r->rcx, g[G_RSP] = r->rsp;
	g[G_RIP] = r->rip, g[G_EFL] = r->rflags, g[G_CSGSFS] = 0x23;
	g[G_ERR] = r->error, g[G_TRAPNO] = r->vector, g[G_CR2] = addr;
	*(uint64_t *)(uc + UC_MASK) = old_mask;
	*(uint64_t *)(uc + UC_FPPTR) = fp_user;
	fpu_save(fp, 1); /* (XSAVE : jamais XSAVEOPT hors de la zone du thread) */
	if (fpu_xcr0) {
		uint32_t *sw = (uint32_t *)(fp + FP_SW);
		sw[0] = FP_XSTATE_MAGIC1;
		sw[1] = fpu_size + 4; /* extended_size */
		*(uint64_t *)&sw[2] = fpu_xcr0; /* xfeatures */
		sw[4] = fpu_size; /* xstate_size */
		*(uint32_t *)(uc + UC_FP + fpu_size) = FP_XSTATE_MAGIC2;
	}
	memcpy(uc + UC_FP, fp, fpu_xcr0 ? fpu_size : 512);
	int bad = copy_to_user(sp, b, size);
	kfree(fmem);
	kfree(b);
	if (bad)
		return -1;
	r->rsp = sp;
	r->rip = handler;
	r->rdi = (uint64_t)sig;
	r->rsi = sp + 8;
	r->rdx = sp + 8 + SI_SIZE;
	r->rax = 0;
	r->rflags &= ~(1ULL << 10); /* DF éteint à l'entrée du gestionnaire */
	return 0;
}

int arch_signal_return(void *frame, uint64_t *mask)
{
	struct regs *r = frame;
	uint64_t uc_addr = r->rsp + SI_SIZE;
	uint8_t uc[UC_FP];
	if (copy_from_user(uc, uc_addr, UC_FP))
		return -1;
	void *fmem;
	uint8_t *fp = fpu_area_alloc(&fmem);
	if (!fp)
		return -1;
	/* l'état FPU : la zone FXSAVE, puis le reste d'XSAVE si la trame le dit (magies, tailles) */
	int xs = 0;
	if (copy_from_user(fp, uc_addr + UC_FP, 512)) {
		kfree(fmem);
		return -1;
	}
	uint32_t *sw = (uint32_t *)(fp + FP_SW);
	if (fpu_xcr0 && sw[0] == FP_XSTATE_MAGIC1 && sw[4] == fpu_size && sw[1] == fpu_size + 4) {
		uint32_t m2 = 0;
		if (copy_from_user(fp + 512, uc_addr + UC_FP + 512, fpu_size - 512) ||
		    copy_from_user(&m2, uc_addr + UC_FP + fpu_size, 4)) {
			kfree(fmem);
			return -1;
		}
		xs = m2 == FP_XSTATE_MAGIC2;
	}
	uint64_t *g = (uint64_t *)(uc + UC_GREGS);
	r->r8 = g[G_R8], r->r9 = g[G_R9], r->r10 = g[G_R10], r->r11 = g[G_R11];
	r->r12 = g[G_R12], r->r13 = g[G_R13], r->r14 = g[G_R14], r->r15 = g[G_R15];
	r->rdi = g[G_RDI], r->rsi = g[G_RSI], r->rbp = g[G_RBP], r->rbx = g[G_RBX];
	r->rdx = g[G_RDX], r->rax = g[G_RAX], r->rcx = g[G_RCX], r->rsp = g[G_RSP];
	r->rip = g[G_RIP];
	/* drapeaux : ceux que le programme peut changer (pas IF, IOPL…) */
	const uint64_t user_fl = 0x0CD5 | (1ULL << 18) | (1ULL << 21);
	r->rflags = (r->rflags & ~user_fl) | (g[G_EFL] & user_fl) | 0x200;
	r->cs = 0x23;
	r->ss = 0x1B;
	if (r->rip >= 0x0000800000000000ULL || r->rsp >= 0x0000800000000000ULL) {
		kfree(fmem);
		return -1;
	}
	*(uint32_t *)(fp + 24) &= 0xFFFF; /* MXCSR : bits réservés à 0 (sinon #GP) */
	if (xs) {
		/* en-tête d'XSAVE (forme standard) : XSTATE_BV dans XCR0, XCOMP_BV et le reste à 0
		 * (sinon #GP dans le noyau) */
		*(uint64_t *)(fp + 512) &= fpu_xcr0;
		memset(fp + 520, 0, 56);
		fpu_restore(fp);
	} else {
		/* une trame sans XSAVE (FXSAVE seul) : x87 et SSE */
		__asm__ volatile("fxrstor64 (%0)" ::"r"(fp) : "memory");
	}
	kfree(fmem);
	*mask = *(uint64_t *)(uc + UC_MASK);
	return 0;
}

void syscall_entry_c(struct regs *r)
{
	struct thread *t = sched_current();
	t->uregs = r;
	t->frame_replaced = 0;
	uint64_t a[6] = {r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9};
	int64_t ret = syscall_dispatch(r->rax, a);
	if (!t->frame_replaced)
		r->rax = (uint64_t)ret;
	signal_user_return(r);
}
