# Étude : réseau et tubes sans aller et retour (anneaux partagés)

Demandée le 21/11/2026, après la lenteur de `git clone` sur le ZBook (500 Ko/s, puis 1,5 Mo/s, quand pkg
télécharge à 16 Mo/s). § 3 (sockets) fait le 22/11 (pc 2026.11.83) : § 6. § 4 (tubes) fait le 22/11 : § 7.

## 1. Où en est-on (21/11)

Un programme relié à libposix parle à netd (sockets) et à vfs (tubes) par des messages de 512 octets : un
aller et retour par `recv`, par `poll`, par lecture ou écriture de tube. Corrigé le 21/11 (pc 2026.11.82,
git 2.51.3 puis 2.51.4, bash 1.0.8, smbd 1.0.6, linuxhost 1.0.5) :

| Avant | Maintenant |
|---|---|
| `poll`/`select` sondent chaque socket (recv de 2 Ko) puis dorment 1 ms en cédant le cœur (`SYS_YIELD` en boucle) | netd attend pour eux (op 18 `poll`, futex `net_gen`), NetKit `net_poll` ; un serveur au repos : 3 039 → 8 appels/s |
| `recv` rend la sonde de 2 Ko seule | complétée sans attendre par ce qui est arrivé ; sonde de 8 Ko ; lecture anticipée de 8 Ko pour les petites demandes (TLS) |
| tubes : 464 octets par message | `V_PIPE_READ_BULK` / `V_PIPE_WRITE_BULK` : 64 Ko par message (vfs lit et écrit la mémoire du programme par `SVC_MAP_CALLER`) |
| netd de `/sys/svc` ancien (attentes en `SYS_YIELD` sur `net_lock`) | netd actuel (attentes sur futex) |

QEMU PC (sans KVM), `git clone --no-checkout` de 30 Mo : 62 s → 20,6 s en HTTP, 16,8 s en HTTPS. ZBook : 0,5 →
1,5 Mo/s (avant la lecture anticipée et git 2.51.4). Reste un aller et retour par 8 Ko de socket (NetKit) et
par 64 Ko de tube, plus le `poll` de curl avant chaque lecture.

## 2. Le but

Qu'un programme lise et écrive ses sockets et ses tubes **sans message** tant qu'il y a des données ou de la
place : un anneau en mémoire partagée par socket et par tube, le service (netd, vfs) n'étant appelé que pour
créer, fermer, et attendre quand l'anneau est vide (ou plein).

## 3. Sockets : un anneau de réception par socket, partagé avec le process

- NetKit a déjà un tampon partagé par process (`NET_CLI_BUF`, 128 Ko, 16 zones de 8 Ko), mappé par
  `SYS_BUF_MAP`. On y ajouterait (ou dans un tampon de plus) un anneau de réception par socket ouvert :
  `head` écrit par netd, `tail` par le process, les octets entre les deux.
- **netd** recopie les données TCP reçues de l'anneau de la socket (`rxbuf`) vers l'anneau du client au fil
  de l'eau (ou y reçoit directement : `rxbuf` devient l'anneau partagé), et calcule la fenêtre TCP annoncée
  sur la place libre de cet anneau.
- **recv** (NetKit, puis libposix) : lit entre `tail` et `head` sans appel ; vide : un appel d'attente
  (comme `op_poll`, sur `net_gen`). **poll** : regarde `head != tail` dans la mémoire partagée avant de
  demander à netd.
- **Mise à jour de fenêtre** : quand le process libère beaucoup de place (la moitié de l'anneau), il le dit à
  netd (un avis sans réponse), qui envoie l'ACK de fenêtre.
- Émission : de même, un anneau d'émission, netd réveillé par un avis (`SVC_SIGNAL`) quand il était vide.
- Réveils entre process : le futex du noyau a pour clé (espace d'adresses, adresse virtuelle) : un futex
  dans une page partagée ne réveille pas l'autre process. Il faut soit des futex partagés (clé = adresse
  physique pour les pages partagées), soit `SVC_SIGNAL` / `HANDLE_WAIT`.

## 4. Tubes : l'anneau partagé entre l'écrivain et le lecteur

- vfs crée le tube et donne aux deux bouts une même page d'anneau (64 Ko + en-tête), puis ne sert plus
  qu'aux fermetures, aux attentes et à l'héritage (fork, exec, entrées / sorties standard).
- Lecture : `tail` → `head` sans appel ; vide : attente (futex partagé, ou un message à vfs qui répond quand
  l'écrivain avance). Écriture : de même, attente quand plein (aujourd'hui l'écrivain dort 1 ms et réessaie).
- Plusieurs écrivains (bash, ses enfants) : un verrou dans l'en-tête, ou les écritures de plus de PIPE_BUF
  par vfs comme aujourd'hui.
- Base possible : les canaux locaux du noyau de libposix (`socketpair` AF_UNIX, ipc.c, `HANDLE_WAIT`).

## 5. Ce qu'il faut décider

1. Futex partagés dans le noyau (PC et Pi), ou réveils par messages ?
2. Les sockets d'abord (git, curl, Python, Claude Code), les tubes ensuite ?
3. Garder les messages comme repli (NetKit en asm du Pi, programmes reliés à une ancienne libposix) : oui,
   comme pour `net_poll` et les tubes en bloc.

Mesurer d'abord sur le ZBook ce qui reste (sysstat pendant un clone : appels par seconde de
`git-remote-https`, `git`, `index-pack`, netd, vfs) : si netd et vfs ne sont plus les goulots, ce chantier
peut attendre.

## 6. Fait le 22/11 : les sockets (§ 3)

Choix de l'utilisateur : les sockets d'abord, réveils par futex partagés. Le noyau du PC en avait déjà (une page
`VM_SHARED`, comme les tampons `SYS_BUF_*`, a pour clé son adresse physique) : rien à changer dans le noyau.

- **Tampon du process** (`op_open` de netd) : les zones de NetKit (128 Ko, inchangées), une page d'en-têtes (la
  sonnette du process et ses dormeurs, puis 64 octets par anneau : `head`, `tail`, `seq`, `flags`, `waiters`), 8
  anneaux de 64 Ko. Un NetKit plus ancien n'en voit que les zones. Le Pi : `NET_RINGS` 0.
- **netd** : op 19 (`b` = 0 : un anneau pour cette socket ; 1 : relancer) ; `ring_pump` (thread de la pile, après
  chaque lot de trames et les minuteries) verse l'anneau interne dans celui du process ; plein : `NRF_STALL`, les
  données attendent, la fenêtre TCP suit ; `NRF_FIN`, `NRF_ERR` ; un recv par message lit l'anneau (`ring_read`) ;
  le process de l'anneau ferme sa copie : les octets non lus repassent devant l'anneau interne (`ring_release`).
- **NetKit** : `net_recv` lit l'anneau et dort sur `seq` ; `net_poll` regarde les anneaux sur place et dort sur la
  sonnette ; les sockets en écoute n'ont pas d'anneau ; dans le fils d'un fork, les messages.
- **Mesures (QEMU PC)** : `wget` de 30 Mo 7,2 → 5,6 s ; netd pendant un clone HTTPS 96 000 → 40 000 appels/s ;
  `ringfork` (fork après une lecture) : les 99 990 octets, dans l'ordre.

## 7. Fait le 22/11 : les tubes (§ 4)

- **Un tampon par tube, à la demande** (pas un tampon pour tous : chaque process verrait tous les tubes, et ce qui y
  passe, `secret get | …` compris). `V_PIPE_RING` (56) : vfs crée le tampon du tube (`SYS_BUF_CREATE`, 4 Ko
  d'en-tête + 64 Ko), y verse ce qu'il contient déjà, l'autorise au process qui le demande s'il tient un bout du tube
  (`SYS_BUF_GRANT` : un seul pid à la fois, le client redemande si un autre l'a pris entre-temps). Au plus 24 anneaux
  (`PIPE_RINGS`) : le noyau n'a que 128 tampons pour tout le système. libposix ne le demande qu'après 64 Ko passés
  par messages sur la poignée : les petits tubes (`$(…)` de bash) n'en prennent pas. Le Pi : `PIPE_RINGS` 0 (pas de
  futex partagés), `E_INVAL`, les messages.
- **En-tête** (`struct pipe_ring`, `src/svc/common/fssvc.h` ; le même dans `src/lib/posix/armpi.h`) : `head`,
  `tail`, `wseq` / `rseq` (futex : données écrites, place rendue, et tout changement des bouts) avec leurs dormeurs,
  `wlock` / `rlock` (pid + 1 de qui copie : bash et ses fils tiennent souvent un même bout), `readers`, `writers`
  (vfs seul les écrit : la fin, `EPIPE`), `vfs_wait` (des lecteurs par messages attendent dans vfs : l'écrivain de
  l'anneau lui envoie `V_PIPE_KICK`, 57).
- **vfs** garde les compteurs de bouts et sert toujours les clients par messages (commandes en asm, AppKit, ancienne
  libposix) sur le même anneau, sous les mêmes verrous (pris sans jamais dormir ; occupé : `E_AGAIN`, le client
  redemande) ; il rend les verrous d'un process mort ; le tampon part avec le dernier process qui l'a mappé (le fils
  d'un `fork` le garde, à la même adresse).
- **libposix** : lecture et écriture sans message ; attente sur le futex (100 ms au plus par tour) ; une écriture de
  `PIPE_BUF` octets au plus ne se coupe pas (par messages aussi : vfs coupait une écriture de 4 Ko quand la place
  manquait, deux écrivains se mêlaient) ; `poll` regarde l'anneau sans vfs.
- **Entrées / sorties standard** : quand vfs dit que 0, 1, 2 sont des tubes (et pas des fichiers), libposix en fait des
  bouts de tube à lui (64 Ko par message, puis l'anneau) au lieu de passer par AppKit (464 octets par message, un
  somme de 2 ms quand le tube est plein) : `git fetch-pack` → `index-pack`, les sorties des commandes lues par Claude
  Code. Une poignée tenue par deux descriptions (celles de tty.c remplacées par celles passées par l'exec) n'est
  rendue à vfs qu'avec la dernière.
- **Mesures (QEMU PC)** : `ringtest gen 64 | ringtest check 64` 24,4 → 84,6 Mo/s ; deux process d'un `fork` : 94 Mo/s ;
  `ringtest` (`src/lib/posix/test/ringtest.c`) : flux vérifié, deux écrivains, fin, EPIPE, poll, O_NONBLOCK ; posixtest
  mêmes 15 échecs, pipetest 31/31. Les commandes en asm restent lentes (`ringtest gen 16 | wc -c` : 27 → 16 s) :
  AppKit et ses 464 octets par message, à reprendre à part.
