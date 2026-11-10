; ============================================================================
;  syscalls.inc - numeros des appels systeme (docs/ABI.md)
;
;  Seule source des numeros : incluse par armpi.inc (programmes, kits,
;  services) et par le noyau (src/kernel/syscall.asm, table syscall_table).
;  Groupes par domaine sur le chiffre hexadecimal de tete, sans trou dans
;  chaque plage ; la place libre en fin de plage sert aux appels a venir.
;
;    0x00-0x0F  memoire            0x40-0x4F  materiel, demarrage (SD0:/sys)
;    0x10-0x1F  process, temps     0x50-0x5F  serveur graphique
;    0x20-0x2F  threads, futex     0x60-0x6F  systeme (console, journal, alea, kits)
;    0x30-0x3F  IPC (services)     0x80-0x8F  fichiers et lancement : AppKit
;                                             (pas le noyau : E_NOSYS par svc 0)
; ============================================================================

; --- 0x00 memoire -------------------------------------------------------------
SYS_SBRK		= 0x00		; (increment) -> ancienne limite du tas
SYS_BUF_CREATE		= 0x01		; (taille) -> numero (tampon partage, mappe chez soi)
SYS_BUF_GRANT		= 0x02		; (numero, pid) : autorise ce process a le mapper
SYS_BUF_MAP		= 0x03		; (numero) -> adresse
SYS_BUF_FREE		= 0x04		; (numero) : demappe chez soi
SYS_DMA_ALLOC		= 0x05		; (taille, &adresse physique) -> adresse (non cachee) ; SD0:/sys
SYS_VM_MAP		= 0x06		; (adresse, taille, droits PROT_*, drapeaux VM_*) -> adresse (region a zero, a la demande)
SYS_VM_UNMAP		= 0x07		; (adresse, taille) -> 0
SYS_VM_PROTECT		= 0x08		; (adresse, taille, droits PROT_*) -> 0
SYS_VM_ADVISE		= 0x09		; (adresse, taille, conseil MADV_*) -> 0
SYS_VM_QUERY		= 0x0A		; (adresse, sortie 32 o) -> 0 la contient / 1 suivante / E_NOMEM
SYS_VM_STATS		= 0x0B		; (pid (0 : soi), sortie 64 o) -> 0 / E_SRCH ; pages a lui, partagees, ...
SYS_VM_PAGEINFO		= 0x0C		; (adresse) -> bits PI_* de la page
SYS_BUF_INFO		= 0x0D		; (numero, sortie 32 o BI_*) -> 0 : adresse physique (contigue), octets, createur, autorise

; --- 0x10 process et temps ----------------------------------------------------
SYS_EXIT		= 0x10		; (code)
SYS_GETPID		= 0x11		; () -> pid
SYS_SPAWN_IMAGE		= 0x12		; (image, taille, params 48 o) -> pid ; par AppKit (spawn)
SYS_WAIT		= 0x13		; (pid) -> code de sortie
SYS_KILL		= 0x14		; (pid)
SYS_PROCLIST		= 0x15		; (tampon, max) -> n ; entrees 64 o : pid, threads, Ko tas, nom[32] (+24), etat (+56)
SYS_PROC_FLAGS		= 0x16		; () -> bits 0-2 : entree/sortie/erreur dans un tube, bit 8 : droits
SYS_SLEEP		= 0x17		; (ms)
SYS_YIELD		= 0x18		; ()
SYS_TIME_MS		= 0x19		; () -> ms depuis le demarrage
SYS_CLOCK_GET		= 0x1B		; (0 : secondes UTC, 1 : decalage de l'heure locale en s, 2 : secondes locales)
SYS_CLOCK_SET		= 0x1C		; (secondes UTC, decalage) -> 0 ; droits de SD0:/sys ; ne recule jamais
SYS_SPAWN_PRELOADED	= 0x1A		; (chemin resolu, taille, params 48 o) -> pid / E_NOENT ; par AppKit (spawn)
SYS_PROC_RIGHTS		= 0x1D		; (pid) -> 1 : programme de SD0:/sys, 0 : non, E_SRCH
SYS_PROC_INFO		= 0x1E		; (pid, sortie 64 o) -> 0 / E_SRCH ; l'entree de SYS_PROCLIST de ce process
SYS_PROCLIST_FROM	= 0x1F		; (tampon, max, depuis) -> n ; comme SYS_PROCLIST, a partir de l'emplacement
					; depuis ; la suite : depuis = (pid de la derniere entree) & 0xFFFF

; --- 0x20 threads et futex ----------------------------------------------------
SYS_THREAD_CREATE	= 0x20		; (entree, sommet de pile, argument) -> tid
SYS_THREAD_EXIT		= 0x21		; (code)
SYS_FUTEX_WAIT		= 0x22		; (adresse, valeur attendue, attente ms : 0 = sans fin) -> 0 / E_AGAIN / E_TIMEDOUT
SYS_FUTEX_WAKE		= 0x23		; (adresse, nombre) -> reveilles
SYS_THREAD_CREATE_EX	= 0x24		; (entree, sommet de pile, argument, TPIDR_EL0, priorite, nom) -> tid
SYS_THREAD_JOIN		= 0x25		; (tid) -> code de sortie du thread
SYS_THREAD_SELF		= 0x26		; () -> tid
SYS_THREAD_SET_PRIORITY	= 0x27		; (tid (0 : soi), priorite THREAD_PRIO_*) -> ancienne priorite
SYS_THREAD_SET_NAME	= 0x28		; (tid (0 : soi), nom) -> 0
SYS_THREAD_KILL		= 0x29		; (tid, code) -> 0 ; le thread meurt a son retour en EL0
SYS_THREAD_LIST		= 0x2A		; (pid (0 : soi), tampon, max) -> n ; entrees THL_* de 64 o
SYS_SIG_ACTION		= 0x2B		; (signal, gestionnaire (0 defaut, 1 ignore), SA_*, masque, retour) -> 0
SYS_SIG_SEND		= 0x2C		; (pid (0 : soi), tid (0 : un thread qui ne le masque pas), signal) -> 0
SYS_SIG_RETURN		= 0x2D		; () : fin d'un gestionnaire, reprend l'ucontext de la trame
SYS_SIG_MASK		= 0x2E		; (SIGM_*, ensemble) -> ancien masque (SIGM_* : voir plus bas)
SYS_SIG_ALTSTACK	= 0x2F		; (sp (-1 : lire), taille (0 : aucune), sortie 16 o ou 0) -> 0

; --- 0x30 IPC : services nommes -----------------------------------------------
SYS_SVC_REGISTER	= 0x30		; (nom, drapeaux) -> service ; programmes de SD0:/sys seulement
SYS_SVC_OPEN		= 0x31		; (nom, attente ms) -> service (canal)
SYS_SVC_CALL		= 0x32		; (canal, message 512 o) -> resultat (reponse dans le message)
SYS_SVC_NEXT		= 0x33		; (canal, message, attente ms) -> numero de requete (0 : aucune)
SYS_SVC_REPLY		= 0x34		; (numero, message)
SYS_SVC_MAP_CALLER	= 0x36		; (requete, adresse, taille, MC_WRITE) -> adresse ici : memoire de l'appelant, pages epinglees (programmes de SD0:/sys)
SYS_SVC_UNMAP_CALLER	= 0x37		; (adresse rendue) -> 0 (fait aussi par SVC_REPLY)
SYS_SVC_CALLER_PHYS	= 0x38		; (adresse rendue, tableau, places) -> pages : adresses physiques, pour le DMA
SYS_SVC_SIGNAL		= 0x35		; (canal, bits) : OU dans le mot de signaux du service, sans bloquer
SYS_SVC_FORWARD		= 0x39		; (numero, canal, message) : la requete prise passe a un autre service, qui repondra a l'appelant

SVC_OP_SIGNAL		= 0xFFFD	; (SVC_NEXT) a = bits leves par SVC_SIGNAL ; sans reponse
SVC_SIG_LOWMEM		= 1		; (noyau, service SVF_MEMORY) rendre de la memoire : une fois par signal

; --- 0x40 materiel et demarrage (programmes de SD0:/sys) ----------------------
SYS_DEV_MAP		= 0x40		; (adresse physique, taille) -> adresse ; peripheriques
SYS_FW_CALL		= 0x41		; (message "property tags") -> 0
SYS_MOD_LANCER		= 0x42		; (nom, arguments) -> pid : un module de initramfs
SYS_MOD_LIRE		= 0x43		; (nom, tampon, max) -> octets : un module texte
SYS_DEMARRAGE_PRET	= 0x44		; () : init leve le verrou du demarrage
SYS_BOOT_FILE		= 0x45		; (nom, image, taille) : fichier donne au noyau ; nom 0 : liste attendue ; init
SYS_KIT_REGISTER	= 0x46		; (nom, image, taille) -> table du kit (AppKit, f_kit_load)
SYS_IMAGE_PRELOAD	= 0x47		; (chemin resolu, image, taille) -> 0 : executable precharge ; image 0 : actif ? (init2, par AppKit)
SYS_IMAGE_FORGET	= 0x48		; (chemin resolu) -> nombre : le fichier change (vfs)
SYS_IRQ_BIND		= 0x49		; (ligne, canal (-1 : IRQ_WAIT), bits) -> 0 ; E_NOSYS : option no_irq
IRQ_SHARED		= 1 shl 63	; (IRQ_BIND, dans bits) ligne partagee : deux pilotes qui l'ont demande
SYS_IRQ_ACK		= 0x4A		; (ligne) -> 0 : ligne demasquee (lignes liees a un service)
SYS_IRQ_WAIT		= 0x4B		; (ligne, attente ms) -> 1 levee, 0 delai, E_INTR
SYS_IRQ_UNBIND		= 0x4C		; (ligne) -> 0

; --- 0x50 serveur graphique (noyau : ws.asm ; serveur : Elegant) ---------------
SYS_WS_REGISTER		= 0x50		; () : devient le serveur graphique
SYS_WS_CALL		= 0x51		; (message 512 o) -> resultat (reponse dans le message)
SYS_WS_NEXT		= 0x52		; (message, attente ms) -> numero de requete (0 : aucune)
SYS_WS_REPLY		= 0x53		; (numero, message)
SYS_WS_POST		= 0x54		; (pid, evenement 32 o)
SYS_WS_DISPLAY		= 0x55		; (infos 40 o) : adresse, largeur, hauteur, pas, adresse physique
SYS_EV_WAIT		= 0x56		; (evenement 32 o, attente ms) -> type (0 : aucun)
SYS_SCREEN_INFO		= 0x57		; (tampon) -> largeur, hauteur, barre
SYS_CLIP_SET		= 0x58		; (tampon, longueur)
SYS_CLIP_GET		= 0x59		; (tampon, max) -> longueur
SYS_WS_SIGNAL		= 0x5A		; (bits) : SVC_SIGNAL vers le serveur graphique
SYS_WS_MODE		= 0x5B		; (largeur, hauteur, infos 40 o) : nouvelle resolution ; E_IO : refusee, la precedente reste

; --- 0x60 systeme -------------------------------------------------------------
SYS_PRINT		= 0x60		; (chaine, longueur) : console du noyau (AppKit : sortie standard)
SYS_KLOG		= 0x61		; (tampon, max) -> octets du journal du noyau
SYS_SYSINFO		= 0x62		; (tampon 80 o) -> 0
SYS_GETRANDOM		= 0x63		; (tampon, octets <= 65536) -> octets aleatoires
SYS_LIB_OPEN		= 0x64		; (nom, version minimale) -> table du kit (= kit_find)
SYS_IMAGE_LIST		= 0x65		; (tampon, max) -> n : images valides, entrees IML_* de 128 o
SYS_IMAGE_CREATE	= 0x66		; (chemin resolu, taille, drapeaux IMG_*) -> fenetre : pages du fichier a lire (par AppKit)
SYS_IMAGE_SEAL		= 0x67		; (fenetre, drapeaux IMS_*) -> 0 : image verifiee et mise en place (par AppKit)
SYS_MEMBARRIER		= 0x68		; (MB_*, drapeaux) -> 0 ; MB_QUERY : commandes connues
SYS_PROC_STATS		= 0x69		; (pid (0 : soi), tampon, places) -> mots : [0] appels systeme faits, [1+n] ceux du numero n
SYS_FORK		= 0x6A		; () -> pid du fils (pere), 0 (fils) ; E_PERM : droits de SD0:/sys ; E_NOMEM
					; (plage 0x10 pleine : ici, avec le futur SYS_EXEC en 0x6B)

; --- 0x70 IPC entre process : poignees, canaux locaux, memoire partagee --------
; (src/kernel/ipc.asm, docs/ABI.md : socketpair, SCM_RIGHTS, memfd / shm_open,
; mmap MAP_SHARED de la couche POSIX ; une poignee = 1..1023, par process)
SYS_HANDLE_CLOSE	= 0x70		; (poignee) -> 0
SYS_HANDLE_INFO		= 0x71		; (poignee, sortie 64 o IPCI_*) -> 0
SYS_HANDLE_WAIT		= 0x72		; (entrees 8 o {poignee, evenements, prets}, n <= 64, attente ms (< 0 : sans fin)) -> pretes
SYS_HANDLE_LIST		= 0x73		; (sortie 16 o {poignee, marque, sorte}, max, 1 : marques effacees) -> n
SYS_HANDLE_INHERIT	= 0x74		; (entrees 8 o {poignee, marque}, n <= 255) : donnees au prochain fils lance
SYS_CHAN_CREATE		= 0x75		; (IPC_SOCK_*, sortie 2 x 32 bits) -> 0 : deux bouts relies
SYS_CHAN_SEND		= 0x76		; (poignee, message IPCM_*, IPC_NOWAIT, attente ms (0 : sans fin)) -> octets
SYS_CHAN_RECV		= 0x77		; (poignee, message IPCM_*, IPC_NOWAIT | IPC_PEEK, attente ms) -> octets (0 : fin)
SYS_CHAN_SHUTDOWN	= 0x78		; (poignee, IPC_SHUT_RD | IPC_SHUT_WR) -> 0
SYS_SHM_CREATE		= 0x79		; (taille, IPC_SHM_SEALABLE) -> poignee (memfd_create)
SYS_SHM_OPEN		= 0x7A		; (nom "/x" <= 55 o, IPC_SHM_CREAT | EXCL | TRUNC | WEAK) -> poignee
SYS_SHM_UNLINK		= 0x7B		; (nom) -> 0
SYS_SHM_CTL		= 0x7C		; (poignee, IPC_SHM_SET_SIZE / ADD_SEALS / PIN / UNPIN, valeur) -> 0
SYS_SHM_MAP		= 0x7D		; (poignee, adresse, taille, droits PROT_*, drapeaux VM_FIXED..., decalage) -> adresse

; --- 0x80 fichiers et lancement : servis par AppKit (fichier_sys) et vfs -------
; Le noyau ne les connait pas (svc 0 : E_NOSYS) ; la macro sys d'armpi.inc les
; envoie a AppKit dans un programme qui l'importe (docs/FICHIERS.md).
SYS_OPEN		= 0x80		; (chemin, drapeaux) -> fd
SYS_READ		= 0x81		; (fd, tampon, n) -> n
SYS_WRITE		= 0x82		; (fd, tampon, n) -> n
SYS_SEEK		= 0x83		; (fd, position, origine 0/1/2) -> position
SYS_CLOSE		= 0x84		; (fd)
SYS_STAT		= 0x85		; (chemin, tampon{taille, attributs})
SYS_READDIR		= 0x86		; (fd, tampon DIRENT) -> 1, 0 en fin
SYS_MKDIR		= 0x87		; (chemin)
SYS_UNLINK		= 0x88		; (chemin)
SYS_CHDIR		= 0x89		; (chemin)
SYS_GETCWD		= 0x8A		; (tampon, 256)
SYS_SPAWN		= 0x8B		; (chemin, argument) -> pid
SYS_SPAWN_PIPE		= 0x8C		; (chemin, arguments, &fd) -> pid ; sortie du fils lisible sur fd
SYS_PRELOAD		= 0x8D		; (chemin) -> 0 : image epinglee (IMAGE_CREATE + lecture + IMAGE_SEAL)
SYS_UNLOAD		= 0x8E		; (chemin) -> nombre : images de ce programme oubliees (IMAGE_FORGET)
SYS_APPKIT_FIRST	= SYS_OPEN
SYS_APPKIT_LAST		= SYS_UNLOAD

; --- memoire : VM_MAP, VM_PROTECT, VM_ADVISE, VM_PAGEINFO ----------------------
PROT_NONE		= 0
PROT_READ		= 1
PROT_WRITE		= 2
PROT_EXEC		= 4
VM_FIXED		= 0x10		; a cette adresse (remplace ce qui y est)
VM_NORESERVE		= 0x4000	; (sans effet : rien n'est reserve d'avance)
VM_POPULATE		= 0x8000	; peuplee tout de suite
VM_FIXED_NOREPLACE	= 0x100000	; a cette adresse, E_EXIST si occupee
VM_COW			= 0x200000	; (SHM_MAP) pages de l'objet en lecture, une ecriture les copie (MAP_PRIVATE)
VM_ARENA_BASE		= 0x5800000000	; regions de VM_MAP : [VM_ARENA_BASE, VM_ARENA_END[
VM_ARENA_END		= 0x6800000000
MADV_NORMAL		= 0
MADV_RANDOM		= 1
MADV_SEQUENTIAL		= 2
MADV_WILLNEED		= 3		; peuple
MADV_DONTNEED		= 4		; rend les pages (a zero au prochain acces)
MADV_FREE		= 8		; (comme DONTNEED)
VMK_ANON		= 1		; (VM_QUERY, sorte) region de VM_MAP
VMK_BSS			= 2		; .bss d'un executable
VMK_STACK		= 3		; pile du thread principal
VMK_SHARED		= 4		; memoire partagee (SHM_MAP ; bits 8-31 de la sorte : page de l'objet au debut)
PI_PRESENT		= 1		; (VM_PAGEINFO) page presente
PI_SHARED		= 2		; partagee (image prechargee, kit, tampon)
PI_COW			= 4		; en copie sur ecriture
PI_WRITE		= 8		; inscriptible (EL0)
PI_EXEC			= 16		; executable (EL0)
PI_LAZY			= 32		; dans une region paresseuse
PI_READ			= 64		; lisible (EL0)
; BUF_INFO : sortie de 32 octets (un tampon partage est d'un seul bloc de pages)
BI_PHYS			= 0		; adresse physique du debut (sous 1 Go avec BUF_LOW)
BI_BYTES		= 8		; taille (pages entieres)
BI_OWNER		= 16		; pid du createur
BI_GRANT		= 24		; pid autorise (0 : aucun)
BI_SIZE			= 32
; VM_STATS : sortie de 64 octets (pages de 4 Ko, sauf VS_LAZY en octets)
VS_OWN			= 0		; pages a lui (copies sur ecriture comprises)
VS_SHARED		= 8		; pages partagees (PTE_SHARED)
VS_LAZY			= 16		; octets reserves dans les regions paresseuses
VS_FAULTS		= 24		; pages remplies a la demande
VS_COWS			= 32		; pages copiees sur ecriture
VS_TABLES		= 40		; tables de pages (L2, L3)
VS_FREE_LOW		= 48		; pages libres, zone basse (< 1 Go)
VS_FREE_HIGH		= 56		; pages libres, zone haute
; IMAGE_LIST : entree de 128 octets
IML_FSIZE		= 0		; taille du fichier
IML_PAGES		= 8		; pages chargees
IML_RWPAGES		= 16		; dont inscriptibles (copie sur ecriture)
IML_REFS		= 24		; references : 1 (la table) + 1 par process lance d'elle
IML_LAZY		= 32		; octets de .bss a la demande
IML_KIND		= 40		; IML_PIN (epinglee), IML_SYS (droits de SD0:/sys)
IML_PATH		= 48		; chemin resolu (80 o)
IML_SIZE		= 128
IML_PIN			= 1
IML_SYS			= 8
; IMAGE_CREATE, IMAGE_SEAL : drapeaux
IMG_PIN			= 1		; image epinglee (gardee apres le dernier process)
IMS_ABORT		= 1		; lecture echouee : l'image est rendue

; --- signaux (SYS_SIG_*, membarrier : src/kernel/signal.asm) --------------------
SIGM_SET		= 0		; (SIG_SETMASK de newlib)
SIGM_BLOCK		= 1
SIGM_UNBLOCK		= 2
SIGM_PENDING		= 3		; -> signaux en attente
SIGM_SUSPEND		= 4		; sigsuspend -> E_INTR
SIGM_WAIT		= 5		; sigwait -> un signal de l'ensemble, retire
SA_SIGINFO_		= 2		; (sans effet : le gestionnaire a toujours les 3 arguments)
SA_ONSTACK		= 4		; sur la pile de SIG_ALTSTACK
SA_NODEFER		= 0x40000000
SA_RESETHAND		= 0x80000000
MB_QUERY		= 0
MB_PRIVATE		= 8
MB_REGISTER		= 16
MB_PRIVATE_SYNC		= 32
MB_REGISTER_SYNC	= 64
MB_KNOWN		= MB_PRIVATE or MB_REGISTER or MB_PRIVATE_SYNC or MB_REGISTER_SYNC
; trame sur la pile du gestionnaire : comme Linux arm64
SGF_SI			= 0		; siginfo (128 o)
SIF_SIGNO		= 0
SIF_CODE			= 8
SIF_ADDR			= 16
SGF_UC			= 128		; ucontext
UCX_SS_SP		= 16
UCX_SS_FLAGS		= 24
UCX_SS_SIZE		= 32
UCX_SIGMASK		= 40
UCX_FAULT		= 176		; uc_mcontext : fault_address
UCX_REGS			= 184		;   x0..x30
UCX_SP			= 432
UCX_PC			= 440
UCX_PSTATE		= 448
UCX_FP			= 464		;   __reserved : fpsimd_context
FPSIMD_MAGIC		= 0x46508001
FPSIMD_SIZE		= 528
UCX_SIZE			= UCX_FP + 1024
SGF_SIZE			= SGF_UC + UCX_SIZE

; --- threads : priorites et liste (SYS_THREAD_*) -------------------------------
THREAD_PRIO_NORMAL	= 0
THREAD_PRIO_HIGH	= 1		; passe devant les threads normaux
THREAD_PRIO_RT		= 2		; temps reel souple : devant tous les autres
THREAD_PRIO_MAX		= 2
; thread_list : entree de 64 octets
THL_TID			= 0
THL_STATE		= 8		; 0 pret, 1 endormi, 2 bloque, 3 mort
THL_PRIO		= 16
THL_CPU			= 24
THL_RUNTIME		= 32		; ms (ticks) consommees
THL_NAME		= 40		; 24 octets
THL_SIZE		= 64

; --- IPC entre process (SYS_HANDLE_*, SYS_CHAN_*, SYS_SHM_* : ipc.asm) ---------
IPC_KIND_CHAN		= 1		; sorte d'une poignee : bout de canal local
IPC_KIND_SHM		= 2		;   objet de memoire partagee
IPC_SOCK_STREAM		= 1		; canaux (valeurs de Linux)
IPC_SOCK_DGRAM		= 2
IPC_SOCK_SEQPACKET	= 5
IPC_NOWAIT		= 1		; CHAN_SEND / RECV : EAGAIN plutot qu'attendre
IPC_PEEK		= 2		; CHAN_RECV : lire sans retirer (ni poignees)
IPC_TRUNC		= 1		; IPCM_FLAGS en sortie : message coupe
IPC_CTRUNC		= 2		;   poignees perdues (place ou table pleine)
IPC_SHUT_RD		= 1		; CHAN_SHUTDOWN
IPC_SHUT_WR		= 2
IPC_POLLIN		= 1		; HANDLE_WAIT (valeurs de poll)
IPC_POLLOUT		= 4
IPC_POLLERR		= 8
IPC_POLLHUP		= 16
IPC_POLLNVAL		= 32
IPC_MAX_IOV		= 64		; iovecs d'un message
IPC_MAX_HANDLES		= 256		; poignees d'un message
IPC_WAIT_MAX		= 64		; poignees d'un HANDLE_WAIT
IPC_INHERIT_MAX		= 255		; poignees donnees a un fils
IPC_SHM_SEALABLE	= 1		; SHM_CREATE (MFD_ALLOW_SEALING)
IPC_SHM_PINNED		= 2		; IPCI_FLAGS : objet epingle (IPC_SHM_PIN)
IPC_SHM_CREAT		= 1		; SHM_OPEN
IPC_SHM_EXCL		= 2
IPC_SHM_TRUNC		= 4
IPC_SHM_WEAK		= 8		;   a la creation : l'objet part avec sa derniere reference
IPC_SHM_SET_SIZE	= 1		; SHM_CTL (ftruncate)
IPC_SHM_ADD_SEALS	= 2		;   (F_ADD_SEALS)
IPC_SHM_PIN		= 3		;   objet a nom faible : il reste sans process
IPC_SHM_UNPIN		= 4
IPC_SEAL_SEAL		= 1		; (valeurs de Linux F_SEAL_*)
IPC_SEAL_SHRINK		= 2
IPC_SEAL_GROW		= 4
IPC_SEAL_WRITE		= 8
; message de CHAN_SEND / CHAN_RECV (32 octets)
IPCM_IOV		= 0		; iovecs {adresse, taille} (16 o chacun)
IPCM_IOVCNT		= 8		; (32 bits)
IPCM_NH			= 12		; (32 bits) poignees : envoi, nombre ; reception, place puis nombre
IPCM_HANDLES		= 16		; entrees de 8 o {poignee, sorte | type << 8} (32 bits chacun)
IPCM_FLAGS		= 24		; (32 bits, sortie) IPC_TRUNC, IPC_CTRUNC
IPCM_SIZE		= 32
; HANDLE_INFO : sortie de 64 octets
IPCI_KIND		= 0		; (32 bits) IPC_KIND_*
IPCI_TYPE		= 4		; (32 bits) canal : IPC_SOCK_*
IPCI_SIZE		= 8		; canal : octets en attente ; memoire : taille
IPCI_STATE		= 16		; (32 bits) canal : IPCS_* ; memoire : IPC_SEAL_*
IPCI_FLAGS		= 20		; (32 bits) memoire : IPC_SHM_SEALABLE, IPC_SHM_PINNED
IPCI_PID		= 24		; canal : pid du createur ; memoire : numero de l'objet
IPCI_CAP		= 32		; canal : taille de l'anneau de reception
IPCI_SIZEOF		= 64
IPCS_PEER_CLOSED	= 1		; IPCI_STATE d'un canal
IPCS_SHUT_RD		= 2
IPCS_SHUT_WR		= 4
IPCS_PEER_SHUT_WR	= 8
