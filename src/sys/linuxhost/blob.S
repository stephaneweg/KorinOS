/* le module d'essai lxtest.so (construit pour Linux), dans linuxhost : --self-test ;
   LXTEST_SO : son chemin, donné par tools/linux/build.sh */
	.section .rodata
	.balign 16
	.globl lxtest_so_start
	.globl lxtest_so_end
lxtest_so_start:
	.incbin LXTEST_SO
lxtest_so_end:
	.section .note.GNU-stack, "", @progbits
