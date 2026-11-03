# WiFi CYW43455 — mise en route sur un vrai Raspberry Pi 4

Pilote : le service `SD0:/sys/svc/wifi` (`src/svc/wifi/`, repris de l'ancien
`src/kernel/wifi.asm`, d'après `docs/etude/03-reseau-materiel.md`, section 2 ; lancé par
hwmanager, clé `soc:wifi`). QEMU n'émule pas la puce : sous QEMU le pilote n'est pas lancé
(et sort aussitôt si on le lance) ; `SD0:/sys/svc/wifi test` simule une puce pour essayer
l'applet et le menu. Tout ce qui suit est **à valider sur la carte**. Réseaux connus,
connexion automatique, service « wifi » : docs/RESEAU.md, section 4 et 10.

## Préparer la carte SD

1. `.\build.ps1 sd E:` copie aussi `/firmware/brcmfmac43455-sdio.{bin,txt,clm_blob}`.
2. Copier `SD0:/etc/wifi.example.txt` en `SD0:/etc/wifi.txt` et y mettre `ssid=` et `key=`
   (la clé peut contenir des espaces ; `country=FR` est facultatif, `pays=FR` d'`options.txt`
   passe avant ; plusieurs réseaux : un `ssid=` par réseau, `priority=` facultatif).
3. Ne pas mettre `dtoverlay=disable-wifi` dans `config.txt`.

## Messages attendus sur la console (UART), dans l'ordre

| Étape | Message | Si ça échoue |
|---|---|---|
| Contrôleur Arasan, horloge 400 kHz | — | « contrôleur SDIO muet » : horloge EMMC (mailbox id 1) ou reset |
| Énumération SDIO (CMD5/3/7, bus 4 bits) | — | « pas de réponse SDIO » : broches GPIO 34-39 en ALT3, alimentation WL_REG_ON (firmware VC) |
| Identification | `WIFI : puce 00004345 révision 6` | « puce inattendue » : fenêtre backplane, accès 32 bits |
| Firmware | `WIFI : firmware 548728 octets` | « chargement du firmware incorrect » : écriture CMD53 F1 en mode bloc |
| Démarrage du CPU de la puce | — | « le firmware ne démarre pas » : vecteur de reset, nvram, horloge HT |
| Ioctls | `WIFI : wl0: Oct ... version 7.45.241` puis `WIFI : MAC ...` | « le firmware ne répond pas » : trames SDPCM sur F2, en-tête BCDC |
| Connexion | `WIFI : connecté` puis `NET : DHCP ok` | `wifi` dans le terminal donne l'erreur (réseau introuvable, clé refusée, délai) |

## Points fragiles connus

* Attente après chaque écriture de registre Arasan (`wl_wdelay`) : à allonger si des
  commandes se perdent.
* Lecture d'une trame en deux CMD53 (12 octets d'en-tête puis le reste arrondi au bloc).
* Crédit d'émission SDPCM : la file de données attend le champ « window » des trames reçues.
* WPA2 : `wsec` = AES + TKIP, `wpa_auth` = WPA2-PSK (WPA-PSK si le scan n'a vu que WPA) ;
  le firmware signale le succès par l'événement PSK_SUP, statut 6.
* Pas d'IRQ : scrutation de l'état de la puce toutes les 1 ms par le thread `wifi`.
* Ni WPA3, ni WEP, ni point d'accès, ni économie d'énergie.
