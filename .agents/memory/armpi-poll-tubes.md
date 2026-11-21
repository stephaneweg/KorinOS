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
