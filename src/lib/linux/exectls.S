/*
 * exectls.S -- liblinux : la réserve de TLS des exécutables de Linux (exec.c ; docs/etude/LINUX-HOST.md § 4.3).
 *
 * Un exécutable de Linux a son bloc de variables de thread juste sous %fs (variante II, décalages
 * « local exec » fixés à l'édition de ses liens), là où l'hôte a déjà le sien. Cette réserve, 64 Ko de
 * .tbss, est placée en DERNIER dans le segment TLS de l'hôte par posix-lx.ld (tools/linux/build.sh) :
 * elle finit à %fs, et le bloc de l'exécutable s'y pose. Elle n'est liée que dans un hôte qui appelle
 * linux_exec (exec.o la demande), pas dans cdmhost.
 */
	.section .lxexe_tls,"awT",@nobits
	.balign 64
	.globl linux_exe_tls_pad
	.type linux_exe_tls_pad, @object
	.size linux_exe_tls_pad, 65536
linux_exe_tls_pad:
	.skip 65536

	.section .note.GNU-stack,"",@progbits
