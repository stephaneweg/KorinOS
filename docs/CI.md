# Intégration continue

But : voir une régression dès le commit fautif. Le workflow `.github/workflows/ci.yml` (GitHub
Actions, exécuteur `windows-latest`, Windows PowerShell comme en local) tourne à chaque push,
à chaque pull request, et à la main (onglet Actions, « Run workflow »).

1. **Assemblage** : `.\build.ps1`, tel quel. FASMARM est dans le dépôt (`tools/fasmarm`), Python
   n'utilise que sa bibliothèque standard. Les parties construites dans WSL hors du dépôt (WebKit,
   OpenSSL, bureautique, émulateurs, GCC, .NET, `pkg`…) ne sont pas refaites : `build.ps1` saute
   déjà ce qui manque dans `build\<partie>\sdroot` (et `mkrepo` les paquets sans fichiers). Ce qui
   est suivi dans git, `prebuilt/` compris, suffit.
2. **Démarrage** : `tools/ci/boot_test.py` lance `build\kernel8.img` dans QEMU raspi4b sans
   affichage, avec les modules de démarrage de la ligne `initramfs` de `boot/config.txt` (comme
   `tools/qemu_drive.py`) et `build\sd.img`, et attend sur l'UART, dans l'ordre :

   | marqueur | texte attendu | source |
   |---|---|---|
   | noyau lancé | `Korin OS - jalon` | `src/kernel/kernel.asm` |
   | init | `Init : services de demarrage (initramfs)` | `src/kernel/kernel.asm` |
   | SD0: servi | `Init : SD0: servi par les services` | `src/kernel/kernel.asm` |
   | noyau prêt | `Noyau : démarrage terminé` | `src/kernel/kernel.asm` |
   | Elegant | `elegant : ecran pris, en service` | `src/sys/elegant` |
   | fin du démarrage | `init2 : preload : ` | `src/svc/init2` |

   Échec immédiat sur `*** EXCEPTION NOYAU`, `] faute, process tué` (un programme planté pendant
   le démarrage), `SD0: non servi`, `pas de module init`, carte SD absente, framebuffer ou mémoire
   d'Elegant en échec ; échec aussi au-delà du délai (240 s en CI, 180 s par défaut ; il faut
   6 s sur un PC). Si un message change dans le code, mettre à jour `MARKERS` / `FATAL` du script.
3. **En cas d'échec** : le journal UART est affiché dans le job, et l'artefact
   `boot-failure-<commit>` (7 jours) garde `ci-uart.log`, l'initramfs, les noyaux, les modules et
   `sd.img`, pour rejouer le démarrage en local.

## En local

```powershell
.\build.ps1 test     # assemble, puis tools/ci/boot_test.py
```

QEMU : `$env:ARMPI_QEMU` s'il est défini (pour `run` et `test`), sinon
`C:\Program Files\qemu\qemu-system-aarch64.exe`. Le journal est dans `build\ci-uart.log`.

## Sous Linux (ou WSL), sans PowerShell

1. **FASMARM pour Linux**, depuis les sources du dépôt et le fasm de `ref/fasm17330` (il donne le même noyau, octet
   pour octet, que `tools/fasmarm/fasmarm.exe`) :
   ```sh
   mkdir -p /tmp/fa && tar -C /tmp/fa -xzf ref/fasm17330/fasm-1.73.30.tgz
   A=tools/fasmarm/source; L=/tmp/fa/arm; mkdir -p $L/linux/x64
   cp /tmp/fa/fasm/source/*.inc $A/*.inc $L/ && cp /tmp/fa/fasm/source/Linux/system.inc $L/linux/
   cp /tmp/fa/fasm/source/Linux/x64/* $A/linux/x64/* $L/linux/x64/
   (cd $L/linux/x64 && /tmp/fa/fasm/fasm.x64 -m 262144 fasmarm.asm /usr/local/bin/fasmarm)
   ```
2. **La carte** : `python -I tools/ci/carte_linux.py` assemble tout le système, les modules et le noyau, pose les
   programmes sur une copie de `sdcard/` et fait `build/sd.img` (ce que WSL construit, WebKit, GCC, .NET, vient de
   `sdcard/` tel quel).
3. **Le démarrage** : QEMU 9.0 ou plus (machine `raspi4b`), puis
   `ARMPI_QEMU=/chemin/qemu-system-aarch64 python -I tools/ci/boot_test.py`.

## Suite

* **Auto-hébergement** : pas encore en CI. `selfhost` s'exécute dans l'OS et assemble ses sources
  sur la carte ; il faudrait le lancer par telnetd (redirection `hostfwd` du port 23), puis
  rapatrier les binaires par ftpd (port 21) et les comparer octet pour octet à ceux de `build\`.
  Compter plusieurs minutes de QEMU pour le noyau et les gros programmes.
* Essais fonctionnels pilotés (`tools/qemu_drive.py` : clics, captures) et réseau
  (`tools/nettest`) : à brancher de la même façon, sans jamais viser de serveur extérieur.
* Le vrai Pi (Wi-Fi, GPU, tactile) reste manuel.
