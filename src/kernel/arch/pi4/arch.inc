; ============================================================================
;  arch/pi4/arch.inc - pilotes du Raspberry Pi 4 (BCM2711)
;
;  Inclus en fin de noyau par kernel.asm (apres le code generique : les
;  macros de celui-ci sont definies). Chemins relatifs a src/, assemblables
;  par FASMARM sous Windows comme sous ARMPI (auto-hebergement).
; ============================================================================

include 'arch/pi4/board.asm'
include 'arch/pi4/gpio.asm'
include 'arch/common/pl011.asm'
include 'arch/pi4/uart.asm'
include 'arch/common/mailbox.asm'
include 'arch/pi4/mmu.asm'
include 'arch/common/smp.asm'
include 'arch/common/fb.asm'

; (le son est sorti du noyau : service sound et ses pilotes, docs/SON.md ;
;  branche de retour son-noyau)
; (le GPU V3D est sorti du noyau : service v3d et GpuKit, docs/V3D.md)
