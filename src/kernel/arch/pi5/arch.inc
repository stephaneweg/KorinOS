; ============================================================================
;  arch/pi5/arch.inc - pilotes du Raspberry Pi 5 (BCM2712 + RP1)
;
;  Etat : demarrage (console, MMU, coeurs par PSCI, GIC, timer, mailbox,
;  framebuffer, carte SD, GPIO du RP1) ecrit d'apres la documentation mais
;  NON TESTE sur un Pi 5 ; le reste est en attente (stubs.asm, docs/PI5.md).
; ============================================================================

include 'arch/pi5/board.asm'
include 'arch/pi5/gpio.asm'
include 'arch/common/pl011.asm'
include 'arch/pi5/uart.asm'
include 'arch/common/mailbox.asm'
include 'arch/pi5/mmu.asm'
include 'arch/common/smp.asm'
include 'arch/common/fb.asm'
include 'arch/pi5/stubs.asm'
