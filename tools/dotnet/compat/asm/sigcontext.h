/* asm/sigcontext.h -- sur ARMPI, dans <sys/ucontext.h> (trame de Linux arm64). Le PC : la trame de Linux
 * x86-64 (src/kernel/arch/x86_64/sigframe.c), dont la PAL lit l'etat XSAVE par ces noms de Linux. */
#include <sys/ucontext.h>

#if defined(__x86_64__) && !defined(_ARMPI_COMPAT_ASM_SIGCONTEXT_X64)
#define _ARMPI_COMPAT_ASM_SIGCONTEXT_X64

#define FP_XSTATE_MAGIC1	0x46505853U
#define FP_XSTATE_MAGIC2	0x46505845U
#define FP_XSTATE_MAGIC2_SIZE	sizeof (FP_XSTATE_MAGIC2)

/* octets 464..511 de la zone FXSAVE */
struct _fpx_sw_bytes
{
	unsigned int magic1;
	unsigned int extended_size;		/* FXSAVE + XSAVE + MAGIC2 */
	unsigned long long xfeatures;
	unsigned int padding[7];
};

struct _header
{
	unsigned long long xfeatures;
	unsigned long long reserved1[2];
	unsigned long long reserved2[5];
};

struct _ymmh_state
{
	unsigned int ymmh_space[64];
};

struct _xstate
{
	struct _libc_fpstate fpstate;
	struct _header xstate_hdr;
	struct _ymmh_state ymmh;
};

#endif
