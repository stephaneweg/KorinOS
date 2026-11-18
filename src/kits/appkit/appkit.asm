; ============================================================================
;  kits/appkit/appkit.asm - AppKit : console, nombres, chaines, tas (docs/KITS.md)
;  Assemble en /lib/appkit.so ; fonctions dans inc/lib.inc et
;  appsA_util.inc (chaines, UTF-8, chemins) ; client de fichiers :
;  fileclient.inc (docs/FICHIERS.md, etape 2).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/vterm.inc'
kit_header
APPKIT_STDIO = 1			; puts : sortie standard (fileclient.inc, etape 4)

segment readable executable
include 'appkit.abi'
include '../../inc/lib.inc'
include 'appsA_util.inc'
include 'fileclient.inc'
include 'lang.inc'			; la langue et les traductions
include 'sync.inc'			; mutex et conditions (futex)

; le coffre (service keyvault, docs/KEYVAULT.md) : seulement dans l'AppKit en C (appkit.c), qui remplace
; celui-ci sur la carte ; ici, les places de la table et E_NOSYS (-38)
vault_set:
vault_get:
vault_delete:
vault_list:
vault_status:
vault_error:
	mov	x0,-38
	ret

segment readable writeable
fileclient_data
lang_data
