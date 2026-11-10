; ============================================================================
;  kits/appkit/appkit.asm - AppKit : console, nombres, chaines, tas (docs/KITS.md)
;  Assemble en SD0:/lib/appkit.so ; fonctions dans inc/lib.inc et
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

segment readable writeable
fileclient_data
lang_data
