; ============================================================================
;  inc/fs.inc - protocole des services de fichiers (docs/MICRO-NOYAU.md,
;  section 4 ; cote noyau : src/kernel/remote.asm, memes numeros)
;
;  Message (512 octets) : +0 op / resultat, +8 a, +16 b, +24 c, +32 d,
;  +40 longueur des donnees, +44 pid de l'appelant, +48 donnees (464 octets).
;    1 ouvrir(a = drapeaux O_* ; donnees : chemin) -> a canal du service (vfs),
;      b numero, c attributs, d taille
;    2 stat(chemin) -> b taille, c attributs, d date (secondes UTC, 0 : aucune)
;    3 mkdir(chemin)          4 unlink(chemin)
;    10 lire(a numero, b position, c octets) -> octets, donnees
;    11 ecrire(a numero, b position, c octets, donnees) -> octets, b taille
;    12 fermer(a numero)      (avis du noyau, pid 0 : sans reponse ; d'un
;      process : appel, reponse 0)
;    13 lire_rep(a numero, b rang) -> 1 (donnees : nom[256], taille, attributs)
;      ou 0 en fin
;    14 tampon() -> a numero du tampon partage de l'appelant (64 Ko, cree et
;      accorde a son pid ; a mapper par buf_map)
;    15 lire_t(a numero, b position, c octets <= 64 Ko) -> octets, au debut
;      du tampon
;    16 ecrire_t(a numero, b position, c octets, au debut du tampon)
;      -> octets, b taille
;    0xFFFF avis de fin (a = pid) : ses fichiers fermes, son tampon rendu
;  Proprietaire d'un fichier ouvert : le pid de l'appelant (+44) ; le relais
;  du noyau (ouvertures transmises par vfs) : SVC_PID_NOYAU. Ops 10-16 :
;  E_BADF pour un autre pid (docs/FICHIERS.md, 4.2).
;    20 monter(donnees : Â« volume source service Â») -> 0   (au service vfs)
;  Le chemin donne au service d'un volume est relatif a sa racine (Â« a/b Â»).
; ============================================================================

FS_OUVRIR	= 1
FS_STAT		= 2
FS_MKDIR	= 3
FS_UNLINK	= 4
FS_UTIME	= 5			; b = secondes UTC ; donnees : chemin
FS_RENAME	= 6			; donnees : ancien, 0, nouveau, 0 (meme volume)
FS_LIRE		= 10
FS_ECRIRE	= 11
FS_FERMER	= 12
FS_LIRE_REP	= 13
FS_TAMPON	= 14
FS_LIRE_T	= 15
FS_ECRIRE_T	= 16
FS_LIRE_D	= 17			; (a numero, b position, c octets, d adresse) : directement dans la memoire de l'appelant -> octets
FS_CACHE	= 18			; (a : 0 lire, FS_CC_*) -> a succes, b defauts, c blocs de 64 Ko, d remplaces
FS_INFO		= 19			; -> a octets du volume, b octets libres (-1 : inconnu), c octets par cluster (0 : memoire), d FS_T_* ; donnees : etiquette
FS_SYNC		= 25			; (au service d'un volume) entrees des fichiers ouverts et
					; FSInfo reecrites -> a fichiers ouverts (eject, mkfs)
FS_T_FAT32	= 1
FS_T_EXFAT	= 2
FS_T_RAM	= 3
FS_CC_ON	= 1
FS_CC_OFF	= 2
FS_CC_DROP	= 3
FS_DIRECT_MIN	= 65536			; octets : en dessous, le tampon partage
FS_DIRECT_MAX	= 0x100000		; une requete (MC_BYTES du noyau)
FS_TAMPON_TAILLE = 65536		; tampon partage d'un process
SVC_PID_NOYAU	= 0x7FFFFFFF		; MSG_PID : un thread du noyau
VFS_MONTER	= 20
VFS_VOLUMES	= 22			; -> b = nombre ; donnees : nom 16, source 24, service 16
VFS_PATHMAP	= 23			; donnees : chemin virtuel, 0, chemin physique, 0 (SD0:/etc/paths.txt)
VFS_PATHQ	= 24			; donnees : chemin -> 1 s'il passe par un chemin virtuel, sinon 0
VFS_DEMONTER	= 21			; donnees : volume (« usb0 ») : son service s'arrete
FS_DONNEES	= 464
; vfs, chemins des programmes (docs/FICHIERS.md, 4.1) : chemin tel que le
; programme l'ecrit (« SD0:/a », « sd:a », relatif, « /a » = racine du volume
; courant), resolu avec le dossier courant du pid appelant
V_OUVRIR	= 30			; a = drapeaux -> a canal, b numero, c attributs, d taille
V_STAT		= 31			; -> b taille, c attributs, d date (secondes UTC, 0 : aucune)
V_MKDIR		= 32
V_UNLINK	= 33
V_CHDIR		= 34			; (du noyau : b = pid concerne)
V_GETCWD	= 35			; -> donnees : « SD0:/a »
V_EXEC		= 36			; comme 30 (lecture) + donnees : nom du process
; vfs, tubes et entrees / sorties standard (docs/FICHIERS.md, etape 4) :
; poignees de bouts de tube, propres au pid appelant (E_BADF sinon)
V_PIPE		= 37			; -> a bout de lecture, b bout d'ecriture
V_PIPE_READ	= 38			; a poignee, b octets (<= 464), c bit 0 : sans attente
					; -> octets (donnees), 0 en fin, E_AGAIN (vide)
V_PIPE_WRITE	= 39			; a poignee, donnees -> octets acceptes (0 : plein), E_PIPE
V_PIPE_CLOSE	= 40			; a poignee
V_UTIME		= 42			; b = secondes UTC ; chemin
V_FILE_END	= 44			; a mode (1 lecture, 2 ecriture) ; chemin -> a poignee d'un bout-fichier
V_END_FILE	= 45			; a poignee -> 1 : un fichier (donnees : chemin, b mode), 0 : un tube
V_RENAME	= 43			; donnees : ancien, 0, nouveau, 0 ; E_XDEV : volumes differents
E_XDEV		= -18
V_STDIO		= 41			; -> a entree, b sortie, c erreur standard (0 : console ; VT_TAG : bout de vterm), d bits : ce sont des fichiers
PIPE_NOWAIT	= 1			; V_PIPE_READ, c : rendre E_AGAIN plutot qu'attendre
E_PIPE		= -32
MSG_A		= 8
MSG_B		= 16
MSG_C		= 24
MSG_D		= 32
MSG_LEN		= 40
MSG_PID		= 44
MSG_DATA	= 48
MSG_TAILLE	= 512

E_NOENT		= -2
E_BADF		= -9
E_EXIST		= -17
E_NOTDIR	= -20
E_ISDIR		= -21
E_INVAL		= -22
E_NOSPC		= -28
E_NOTEMPTY	= -39
E_NOMEM		= -12
ATTR_FICHIER	= 0x20
