# Les sondes du système, et ce que le bus peut remplacer

Inventaire du 4 novembre 2026 : chaque endroit où un programme regarde régulièrement si quelque
chose a changé. Trois sortes :

* **État d'un autre programme ou d'un fichier** : le programme redemande, sur un minuteur, ce
  qu'un autre sait déjà. C'est ce que le bus remplace (docs/BUS.md) : celui qui sait publie,
  les autres s'abonnent.
* **Matériel sans interruption** : le pilote doit continuer à scruter, mais il peut publier ce
  qu'il trouve, pour que personne d'autre n'ait à le redemander.
* **Minuteurs légitimes** : horloge, animation, compteurs. Rien à changer.

Chemins relatifs à `src/`. Les périodes viennent du code.

## Déjà sur le bus

| Quoi | Avant | Maintenant |
|---|---|---|
| Thème du bureau | relance de voronoy, menubar, panel | `korin.theme`, redessin sur place (UIKit, `WEV_THEME`) |
| Volumes dans Fichiers | `VFS_VOLUMES` chaque seconde | `korin.storage.volume.*` (sondage gardé sans busd) |
| Périphériques USB | — | `korin.usb.device.<n>` |
| Réseau dans la barre de menus, le menu Wi-Fi, l'applet Wi-Fi (1, 8, 9) | `net_info`, `wifi_status` (signal lu sur la puce) chaque seconde | `korin.net.state` (netd), `korin.net.wifi` (pilote) |
| Son dans la barre de menus et le Mixer (2, 7) | `ak_state` (appel à sound) toutes les 200 ms | `korin.audio.state` (sound) ; le volet du son (6) lit la mémoire partagée, sans appel |
| Heure dans la barre de menus, l'Horloge, clockd (4, 5) | `clock.cfg` relu chaque seconde ; l'Horloge ne le relisait jamais | `korin.clock.offset` (netd, l'Horloge) |
| Disques, usbstorage (3, 21) | `VFS_VOLUMES` chaque seconde ; usbd interrogé chaque seconde | `korin.storage.volume.*`, `korin.usb.device.<n>` |

## État d'un autre programme : à passer sur le bus

Du plus utile au moins utile.

| # | Où | Ce qui est sondé | Période | Éditeur → sujet |
|---|---|---|---|---|
| 1 | `sys/menubar/menubar.asm` (`net_icon`) | netd `net_info`, `wifi_status`, `net_active` : trois appels à chaque dessin | chaque seconde (l'horloge redessine) | netd → `korin.net.state` |
| 2 | `sys/menubar/menubar.asm` (`snd_poll`, `ak_state`) | génération d'état du son (muet, sortie) | 200 ms (`MB_POLL`) | sound → `korin.audio.state` |
| 3 | `apps/disks/main.cpp` (`read_volumes`) | vfs `VFS_VOLUMES` | 1 s | vfs publie déjà `korin.storage.volume.*` : à faire comme Fichiers |
| 4 | `sys/menubar/menubar.asm` (`load_offset`) | relit `SD0:/etc/clock.cfg` (écrit par netd après NTP ou changement de fuseau) | 1 s | netd, Horloge → `korin.clock` |
| 5 | `apps/clock/clock.asm` (`load_cfg`) | `clock.cfg` lu une fois : un fuseau changé n'est pas vu | jamais relu | même sujet que 4 |
| 6 | `sys/soundmenu/soundmenu.asm` (`refresh`) | génération d'état du son | 200 ms | sound → `korin.audio.state` |
| 7 | `apps/mixer/mixer.asm` | `ak_state` | 200 ms | sound → `korin.audio.state` (les vumètres restent périodiques) |
| 8 | `sys/wifimenu/wifimenu.asm` (`tick`) | wifi `wifi_status` | 1 s | wifi → `korin.net.wifi` |
| 9 | `apps/control/applets/wifi.inc` (`wf_tick`) | `wifi_status`, `net_info` | 1 s | wifi, netd → `korin.net.wifi`, `korin.net.state` |
| 10 | `apps/irc/main.cpp` (`net_watch`) | `net_info` : le réseau est-il là ? | 1 s | netd → `korin.net.state` |
| 11 | `sys/pkgd/main.cpp` | `net_up()`, `time_set()` | 15 min | netd → `korin.net.state`, `korin.clock` |
| 12 | `kits/netkit/netkit.asm` (`wait_dhcp`) | `net_info` jusqu'à l'adresse DHCP | 200 ms, 20 s au plus | netd → `korin.net.state` (`BUS_CURRENT`) |
| 13 | `sys/clipboard/clipboard.asm` (`read_ring`) | numéro de série de clipd | 250 ms | clipd → `korin.clipboard` |
| 14 | `apps/control/applets/printers.inc` (`pr_tick`) | printd `print_jobs` | 1 s | printd → `korin.print.jobs` |
| 15 | `sys/panel/panel.asm` (`refresh`, `SYS_PROCLIST`) | la liste des process (apps lancées) | 1 s | aucun éditeur aujourd'hui : il faudrait annoncer chaque lancement et chaque fin (noyau ou init2) → `korin.proc.*` |
| 16 | `apps/notes/main.cpp` (`poll_open`, `poll_card`) | fichier de passage `NOTES_OPEN`, date de `notes.ini` | 1 s | Notes → `notes.open`, `notes.changed` |
| 17 | `sys/stickies/stickies.asm` | relit `notes.ini` | 30 s (en secours de l'op 2 de Notes) | Notes → `notes.changed` |
| 18 | `apps/calendar/main.cpp` (`ics_stamp`) | date de `calendar.ics` (PocketQI l'écrit) | 500 ms | PocketQI, Calendrier → `calendar.changed` |
| 19 | `apps/control/applets/preload.inc` (`pl_status`) | `SYS_IMAGE_LIST` | 1 s | preload, unload → `korin.preload` |
| 20 | `sys/menubar/menubar.asm` (`menu_poll`) | menu de la fenêtre active | 200 ms | Elegant : plutôt un événement du serveur graphique (menu changé) qu'un sujet |
| 21 | `svc/usbstorage/usbstorage.asm` (veille) | demande à usbd si la clé est encore là | 1 s | usbd retire déjà `korin.usb.device.<n>` : s'y abonner |
| 22 | `svc/gpio/gpio.asm` (`touch_runs`) | `SYS_PROCLIST` : gt911 tourne-t-il ? | à la demande, gardé 1 s | gt911 → `korin.input.touch` |
| 23 | vncd, rdpd, telnetd, ftpd | réessaient la prise réseau tant que netd est absent ou relancé | 2 s | netd → `korin.net.state` |
| 24 | `kits/gpukit/gpukit.asm` | réessaie d'ouvrir v3d | 1 s | mineur : v3d → `korin.gpu` |
| 25 | `svc/inc/netif.inc`, `svc/inc/sndif.inc` | battement des pilotes vers netd et sound (se réinscrire s'il a été relancé) | 2 s | mineur : un sujet « service relancé » |

## Relances et relectures forcées

* `apps/control/applet.inc` (`relaunch` : tue puis relance) : les applets Dock et Panel relancent
  **panel** après avoir écrit `dock.ini` ou `panel.ini`. Avec un sujet `korin.panel.config`, le
  Panel relirait sur place, comme pour le thème.
* **Langue** (`applets/keyboard.inc`) : `lang=` dans `system.ini` n'est lu qu'au lancement d'un
  programme ; les apps ouvertes ne changent jamais. Sujet : `korin.locale`.
* **Fuseau horaire** : écrit, puis NTP ; seule la barre de menus le voit (point 4), pas
  l'Horloge (point 5).
* Appels directs qui marchent déjà comme des sujets, mais pour un seul lecteur : Notes → notes
  épinglées (ops 2 et 3), Calendrier → agenda (op 1), panneau Manette → HIDManager
  (`HID_OP_PADMAP`).
* Les ports C++ (Calendrier, Fiches, IRC, Lettres, Mail, Media, Paint, Sheet, Slides, Telegram,
  Turtle, Cosmo Zoom, Courier, QBStudio, Circuits) lisent les jetons du thème au lancement
  seulement : aucun ne traite `WEV_THEME`.

## Matériel sans interruption : le pilote garde sa sonde, et publie

| Où | Ce qui est scruté | Période | Sujet pour le résultat |
|---|---|---|---|
| `svc/netd/background.inc` | son propre état DHCP et IP (il envoie déjà la notification « connecté / perdu ») | 500 ms | **l'endroit naturel pour publier `korin.net.state`** |
| `svc/wifi/wifi.asm` (`conn_thread`) | sa connexion (reconnexion automatique) | 250 ms | `korin.net.wifi` |
| `svc/genet/genet.asm` | lien du PHY (câble Ethernet) | 2 s | par netd : `korin.net.state` |
| `svc/hwmanager/hwmanager.asm` (`hdmi_poll`) | branchement HDMI (tue ou relance hdmiaudio) | 2 s | `korin.display.hdmi` |
| `svc/hwmanager/hwmanager.asm` | adresses I2C de drivers.txt | 2 s pendant 1 min | le pilote lancé |
| `svc/gt911/gt911.asm` | présence de l'écran tactile, puis touchers | 2 s pendant 1 min ; 16 ms au toucher | `korin.input.touch` |
| `sys/printd/main.cpp` (`follow`) | état d'une impression sur l'imprimante réseau | 3 s | `korin.print.job.<n>` |
| `svc/usbd/usbd.asm` | ports USB | 2 ms | publie déjà `korin.usb.device.<n>` |
| `svc/hidmanager/hidmanager.asm` | anneaux des pilotes d'entrée | 10 ms (50 au repos) | aucun (flux) ; manettes : `korin.input.pad` |
| `svc/gpio/gpio.asm` | fronts des broches demandées | 1 ms | facultatif : `korin.gpio.<n>` |
| pilotes audio, mixage | DMA, anneaux | 2 à 5 ms | aucun (flux) |

## Minuteurs légitimes : rien à changer

* **Horloges et calendriers** : horloge de la barre, application Horloge, minute de l'agenda et
  ses rappels, journée du Calendrier, heure sauvée par init2 toutes les 30 min, NTP toutes les 6 h.
* **Délais** : durée d'une bulle de notifyd, sauvegarde des réglages du son 1 s après le dernier
  changement.
* **Compteurs et médias** : taskman, vumètres du Mixer, position de Music, du lecteur et de
  l'enregistreur, jeux, animations, boucle de composition d'Elegant.
* **Fin d'un travail interne** : processus enfant du terminal, travail de wifimenu, prises de
  Courier, Mail et IRC, relève du courrier.

## Ordre proposé

1. **Réseau** : fait pour 1, 8 et 9 (`korin.net.state`, `korin.net.wifi`). Restent IRC et
   pkgd (C++ : le compilateur de WSL), NetKit (`wait_dhcp`) et les serveurs (10, 11, 12, 23).
2. **Son** : fait (8/11, `korin.audio.state`).
3. **Heure** : fait (8/11, `korin.clock.offset`).
4. **Disques** et **usbstorage** : faits (8/11).
5. **Process** : annoncer les lancements et les fins, pour le Panel (15) et gpio (22).
6. Le reste quand on touche ces programmes : presse-papier, impressions, Notes, Calendrier,
   langue, réglages du Panel.
