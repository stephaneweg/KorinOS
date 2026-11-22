/*
 * korin.h -- ce que korin-mcp lit du système : appels système (numéros et dispositions tirés de src/inc/<nom>.inc par
 * tools/mcp/build.sh dans korin_consts.h, la seule source), NetKit par sa table (rangs tirés de netkit.abi), et
 * les services (vfs, fs.<volume>, intelgpu) par leurs messages de 512 octets.
 */
#ifndef KORIN_MCP_KORIN_H
#define KORIN_MCP_KORIN_H

#include <stdint.h>

#include <armpi/kit.h>
#include <armpi/syscall.h>

#include "korin_consts.h"

#define ksys0(n) armpi_syscall((n), 0, 0, 0, 0, 0, 0)
#define ksys1(n, a) armpi_syscall((n), (long)(a), 0, 0, 0, 0, 0)
#define ksys2(n, a, b) armpi_syscall((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define ksys3(n, a, b, c) armpi_syscall((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)

/* un message de service (src/inc/fs.inc : op, a, b, c, d, longueur, pid, 464 octets de données) */
struct kmsg {
	long op, a, b, c, d;
	unsigned len, pid;
	unsigned char data[464];
};

/* le service vfs (libposix : __armpi_vfs_call, le canal ouvert une fois) */
long __armpi_vfs_call(struct kmsg *m);

/* une fonction de NetKit par son rang (0 : kit absent) */
void *netkit_fn(unsigned rank);

#endif
