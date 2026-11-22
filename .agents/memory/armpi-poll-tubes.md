---
name: armpi-poll-tubes
description: "21/11 : git lent sur le ZBook (0,5 Mo/s) : poll de libposix, netd ancien de /sys/svc, tubes à 464 o ; corrigés et publiés ; étude docs/etude/ANNEAUX.md (anneaux partagés) au plan"
metadata:
  type: project
---
Le 21/11/2026, `git clone` plafonnait à 500 Ko/s sur le ZBook (pkg télécharge à 16 Mo/s). L'utilisateur a mesuré sur
place (son Claude Code dans Korin) : netd avec 26 threads à ~100 %, 186 000 appels/s (time_ms, yield), git-remote-https
à 73 % de SYS_YIELD, smbd à 24 000 appels/s au repos.

Causes trouvées :
- le paquet pc avait deux netd : celui de korin/initrd.tar à jour (attente sur futex), celui de /sys/svc ancien, et
  /etc/services.txt lance /sys/svc/netd. Presque tous les services de /sys/svc différaient de ceux de l'initrd
  (pc 2026.11.78) : vérifier lequel est lancé avant de publier un service ;
- poll/select de libposix sondaient chaque socket (recv de 2 Ko) puis dormaient 1 ms, et __posix_sleep_ns finissait
  la milliseconde par SYS_YIELD en boucle ; curl (poll avant chaque lecture) ne recevait que 2 Ko par tour ;
- tubes de vfs : 464 octets par message (git : remote-https -> fetch -> index-pack, deux tubes).

Fait et publié (21/11) : pc 2026.11.79 (netd actuel dans /sys/svc), pc 2026.11.82 (netd op 18 poll sur net_gen avec
regroupement de 2 ms, NetKit net_poll rang 46, vfs V_PIPE_READ_BULK 54 / V_PIPE_WRITE_BULK 55 par SVC_MAP_CALLER,
initrd : seuls vfs, netd, netkit.so remplacés ; etc/pkg/abi 629), git 2.51.3 puis 2.51.4 (lecture anticipée de 8 Ko),
bash 1.0.8, smbd 1.0.6, linuxhost 1.0.5 (Claude Code : epoll repose sur poll). libposix revient aux anciennes voies
si net_poll ou les tubes en bloc manquent (E_INVAL, -38). QEMU PC : git clone 30 Mo 62 s -> 20,6 s (HTTP), 16,8 s
(HTTPS) ; un serveur au repos 3 039 -> 8 appels/s ; smbd 68 appels/s ; pipetest 31/31 ; posixtest mêmes 15 échecs
qu'avant ; linuxhost : epolltest, sigchldtest, spawntest, bashtooltest pipe bons (bashtooltest socket échouait déjà).
ZBook : 1,5 Mo/s avant la lecture anticipée (git 2.51.4 pas encore mesuré).

Pas encore relié à la nouvelle libposix : les autres programmes (python, gcc/libposix.a de /devtools, apps, services
libposix : oauthd, keyvault, webd, pkg…) ; ils gardent l'ancien poll jusqu'à leur prochaine construction.

**How to apply:** banc : scratchpad prep.py + tools/python/pctest.py run ; serveurs de l'hôte tools/git/serve.py
(8091), python -m http.server (8092), relais TLS (8443) ; polltest (serveur au repos). Suite au plan :
docs/etude/ANNEAUX.md (anneaux partagés pour sockets et tubes, futex partagés ?), après une mesure sysstat sur le ZBook.
L'utilisateur a demandé si git natif sur les kits était envisageable : déconseillé (git entier sur POSIX), décision non
prise. Voir [[armpi-chantier-perf]], [[armpi-gh]].

**22/11 (nuit)** : sur le ZBook, clone 1,8 Mo/s avec l'app Claude Code ouverte (même vers /tmp), **11 Mo/s depuis le
Terminal** sans elle : le bruit de fond de Claude Code (linuxhost ~46 000 appels/s, netd 100 000, vfs 49 000) étouffait
netd et vfs. Cause : epoll de libposix en tranches de 5 ms, poll mélangé (eventfd = canaux du noyau, sockets, tubes) à
la ms avec un appel à vfs par tube, et EPOLLET qui tournait à vide sur une socket toujours prête en écriture. Corrigé :
canal de réveil d'epoll (socketpair), EPOLLET déjà dit masqué jusqu'à la fin de sa fenêtre de 50 ms, poll mélangé qui
dort sur les canaux (ou netd) et regarde le reste de 1 à 16 ms. Banc idletest (scratchpad, Linux, sous linuxhost) :
linuxhost 11 502 -> 1 119 appels/s. Publié : linuxhost 1.0.6. Restent à relier : l'app claudecode, python, etc. ; nvme
à optimiser (155 000 appels/s pendant une écriture ext4 ; attente wait_slots par SYS_IRQ_WAIT de 5 ms).

**22/11 (nuit, suite) : anneaux partagés pour les sockets, faits (choix de l'utilisateur : sockets d'abord, futex
partagés)**. Le noyau du PC avait déjà des futex partagés (pages VM_SHARED : clé = adresse physique) ; les tampons
SYS_BUF_* sont VM_SHARED : aucun changement du noyau. netd : tampon du process agrandi (zones 128 Ko + page d'en-têtes
+ 8 anneaux de 64 Ko, NET_RINGS 8 sur le PC, 0 sur le Pi), op 19 (b 0 : anneau pour la socket -> numéro, E_AGAIN pas
encore connectée ; b 1 : relancer), ring_pump depuis le thread de la pile, ring_read pour un recv par message (fils
après fork), ring_release(keep) à la fermeture par le process de l'anneau (octets non lus remis devant rxbuf), réveils
seulement s'il y a un dormeur (waiters). NetKit : net_recv lit l'anneau, net_poll regarde les anneaux sur place et
attend sur la sonnette ; net_listen exclut les sockets en écoute ; net_fork_child : le fils passe aux messages. Banc
ringfork (scratchpad). QEMU : wget 7,2 -> 5,6 s, netd 96 000 -> 40 000 appels/s pendant un clone HTTPS. Publié : pc
2026.11.83. Les tubes en anneau : pas faits (étape suivante possible). ZBook : à mesurer.

**22/11, mesure sur le ZBook (pc .83, linuxhost 1.0.6, git 2.51.4, bash 1.0.8)** : git clone lancé par Claude Code
(bash -> script -> git) **5,8 à 6 Mo/s** (0,5 au départ, 1,8 puis 3, puis 3,8-4 avant les anneaux) ; depuis le Terminal
sans Claude Code : 11 Mo/s (avant les anneaux). Restent : la charge de vfs et ramfs pendant que Claude Code suit une
commande (fichiers de /tmp ?), les tubes en anneau (ANNEAUX.md § 4), les programmes pas encore reliés à la nouvelle
libposix (claudecode, python, Jet et ses apps web : Netflix ouvert = 28 000 appels/s), nvme.

**22/11, trace de Claude Code (LINUX_TRACE_SYS, démarrage)** : presque tout est clock_gettime (TSC de libposix : pas
d'appel au noyau, hors sysstat). Trouvé : **EPOLLONESHOT ignoré** par l'epoll de libposix (stderr inscrit
EPOLLOUT|ONESHOT par Bun -> epoll_wait rendait aussitôt, sans fin, pendant les commandes : suspect n° 1 de vfs/netd à
170 000 appels/s pendant un clone lancé par Claude Code). Corrigé (entrée mise de côté jusqu'au EPOLL_CTL_MOD ; banc
oneshottest du scratchpad). Ajouté LINUX_COUNT_SYS=fichier (compteurs par appel et par chemin, réécrits toutes les 5 s :
l'utilisateur trouvait LINUX_TRACE_SYS trop lent). linuxhost 1.0.7 ; netd (anneau refusé aux sockets fermées) dans pc
.84. À mesurer sur le ZBook : sysstat pendant un clone lancé par Claude Code.

**22/11, ZBook après linuxhost 1.0.7 / pc .84 : git clone lancé par Claude Code à 8-11 Mo/s** (6 avant) ;
LINUX_COUNT_SYS : linuxhost ~1 000 appels de Linux/s (surtout clock_gettime, sans noyau), sysstat 3 162/s. Restaient
nvme 1 545 000/s et netd 1 098 000/s : attentes actives avec un appel par tour (nvme : SYS_TIME_MS à chaque tour de
wait_slots ; netd : SYS_YIELD + 2 SYS_TIME_MS entre deux trames pendant NET_BUSY_MS). Corrigé : d'abord sur place
(pause, cpu_relax) ; nvme lit l'heure tous les 16 tours après 256 tours libres ; netd regarde ses anneaux NET_SPIN
fois avant de céder. QEMU (wget 30 Mo vers le disque) : nvme 61 000 -> 4 800, netd 81 000 -> 17 700, 15,4 -> 13,3 s.
Publié pc .85 (netd et nvme seuls, l'initrd refait depuis celui de .84).
