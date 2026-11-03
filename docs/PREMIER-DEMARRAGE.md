# Le premier démarrage

`src/sys/setup` (`SD0:/sys/setup`) : l'assistant d'une carte neuve, sur UIKit, construit par `tools/pkg/build.sh`.

| Étape | Quoi | Où c'est écrit |
|---|---|---|
| Bienvenue | la langue ; l'assistant repart dans cette langue | `etc/system.ini` : `lang` |
| Région et clavier | le pays (il choisit aussi son fuseau et son clavier), le fuseau (une région, puis une ville parmi les 419 de `res/tz.txt`), le clavier, pris tout de suite | `etc/options.txt` : `pays`, `fuseau`, `keyb` |
| Écran et thème | la résolution, essayée puis gardée ou rendue ; le thème | `etc/options.txt` : `ecran` ; `etc/theme.txt` |
| Disque | étendre une partition sur l'espace libre qui la suit, en créer une (FAT32), en supprimer une (pas celle du système) ; écrit en continuant, puis l'appareil redémarre et Setup reprend au Wi-Fi | la table de partitions ; `etc/mounts.txt` |
| Wi-Fi | les réseaux autour, le mot de passe (une case l'affiche) ; à passer si besoin | `etc/wifi.txt`, par NetKit |
| Logiciels | les paquets du dépôt, à cocher, installés depuis Internet ; le dépôt est lu une fois l'heure reçue par NTP (sans elle, le Pi se croit en 1970 et refuse le certificat), « Réessayer » le relit | la base des paquets |
| Accès à distance | telnet, VNC, RDP, FTP, et leur mot de passe commun, obligatoire si l'un est activé | `etc/services.txt` : une ligne en commentaire est coupée ; `etc/remote.txt` |

## Comment il se lance

`tools/mincard.py` prépare `etc/session` : le fond d'écran et Setup seuls, le reste attend en commentaire. Les lignes de
`etc/preload.txt` attendent de même : le préchargement de Jet retarderait Setup de près de deux minutes.

```
SD0:/sys/voronoy
SD0:/sys/setup
#setup: SD0:/sys/menubar
#setup: SD0:/sys/panel
```

« Démarrer Korin OS » écrit les services, rend les lignes de la session et du préchargement, retire la sienne et redémarre : tout repart dans
la langue et le thème choisis. Pour le revoir, remettre ces lignes.

`tools/mkimages.py` fait la même chose sur la carte complète. `build.ps1 sd`, sur une carte existante, ne touche pas à
sa session.

## Le disque

`src/sys/setup/disque.h` lit la table de `blk.sd0` (MBR, et les tables EBR des partitions logiques). Tout se prépare en
mémoire : « Défaire » revient à la table lue. En quittant l'étape, si quelque chose change, Setup demande confirmation,
écrit `SD0:/var/setup-page` (l'étape où reprendre), formate les nouvelles partitions, écrit les tables et redémarre :
le noyau ne lit la table qu'au démarrage, et les logiciels de l'étape suivante vont ainsi sur la carte agrandie.

| Geste | Règle |
|---|---|
| Étendre | seulement sur un espace libre juste après ; 1 Mo reste libre devant la partition suivante (la place de sa table si elle devient logique) |
| Créer | seulement dans un espace libre, 64 Mo au moins ; FAT32 « KORIN DATA », la taille en Go ou tout l'espace ; montée en `SD<n>:` |
| Supprimer | toute partition sauf celle du système (`blk.sd0p1`, `SD0:`) ; une partition existante demande confirmation |

- **Jusqu'à 4 partitions,** toutes sont primaires. **Au-delà,** celle du système reste `p1` et les autres deviennent
  logiques, dans une partition étendue (type `0x0F`) : `p5` à `p12`, que `src/svc/inc/blkserv.inc` sert.
- **`etc/mounts.txt` suit :** les numéros changés sont réécrits, une partition supprimée passe en commentaire, une
  nouvelle s'ajoute (`SD1 = blk.sd0p5, fatfs`).
- **Agrandir un FAT32 sans rien déplacer :** Setup n'écrit jamais dans une partition montée. Il agrandit la partition
  dans la table, et fatfs agrandit le système de fichiers au montage suivant (`fat_agrandir`, `src/svc/fatfs/fat32.asm`) :
  secteurs du BPB et de sa copie, clusters libres de FSInfo. Cela marche jusqu'à ce que permet la FAT.
  `tools/mkimages.py` taille celle des images pour 128 Go (`mkdisk.py --extensible 128`, 2 × 128 Mo de FAT).
- **La taille de la carte** vient du registre CSD, lu par le pilote (`sd_capacite`, `src/svc/sd/sdhci.asm`).
