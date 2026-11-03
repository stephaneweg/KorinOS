# L'impression

Port de l'impression d'Onyx (`user/Kits/printerkit`, `user/Apps/printd`, `user/Apps/printconf`, `docs/18-PRINTERKIT.md`).

| Quoi | Où | |
|---|---|---|
| **PrinterKit** | `src/kits/printerkit` → `SD0:/lib/printerkit.so` | assembleur ; `include/printerkit.h`, `include/pdfprint.h` depuis le C++ ; `-lprinterkit` |
| **printd** | `src/sys/printd` → `SD0:/sys/printd` | C++, le service `print`, lancé par `SD0:/etc/session` (et par le kit s'il manque) |
| **Imprimantes** | `src/apps/control/applets/printers.inc` | l'applet de Réglages (`control printconf`) |
| Fausse imprimante | `tools/nettest/ippsrv.py` | pour les essais sous QEMU |

Une application imprime en quatre temps : le dialogue, un travail, ses pages, la fin. Les pages sont **enregistrées** dans
`SD0:/var/spool/print/<id>.opj` (le format est en tête de `printerkit.asm`), le ticket est `<id>.job`. Le service rejoue les
pages pour ce qui imprime, et l'application n'attend pas l'imprimante :

* l'imprimante **PDF**, toujours là : les pages en PDF (`pdfwrite.h` de Letters), le texte reste du texte ;
* une imprimante **réseau** (IPP Everywhere, AirPrint, sans pilote, `ipp.h`) : les pages rendues à sa résolution (`raster.h`)
  et envoyées en PWG Raster à mesure, ou en PDF si elle ne prend que cela, puis l'imprimante interrogée jusqu'à la fin.

L'utilisateur est prévenu par une notification.

## Le kit

Longueurs en **millièmes de point** (entiers : les kits n'ont pas de flottants ; `print_mpt` en C), y vers le bas. Un seul
travail à la fois par process. Tout le savoir est chez printd : imprimantes, papiers, polices ; le kit écrit le fichier
des pages, appelle le service (messages de 512 octets, `printd.inc`) et montre le dialogue, en widgets UIKit.

```c
#include <printerkit.h>
printerkit_kit_open ();
PrintSetup s; print_setup_default ((long) &s);
PrintDialogInfo di = { sizeof di, "Mon document", 1, 0, 0, 0, 0 };
if (print_dialog ((long) &s, (long) &di))
{
	long j = print_begin ((long) &s, (long) "Mon document");
	long f = print_font (j, (long) "DejaVu Sans", 0);
	if (print_page (j, 0, 0))
		print_text (j, f, 12000, 72000, 84000, (long) "Bonjour", 0x000000);
	print_end (j);
}
```

Rectangles, traits, tracés (`print_path_*`), images (`print_image` : réduites à 300 points par pouce et compressées dans le
kit ; `print_image_jpeg` : un JPEG tel quel). Une application qui a un export PDF imprime par le même code :
`pdfprint.h`, `PrintWriter` est un `pdfw::Writer` dont les pages vont dans le travail.

## Le service

Deux fils : celui du service répond, l'autre fait les travaux, un à la fois. Les travaux restés dans la file repartent au
démarrage. `SD0:/etc/printers.ini` est écrit par printd quand une imprimante est ajoutée.

Différences avec Onyx :

* **Pas de FreeType** : `ttf.h` lit cmap, hmtx, glyf ; `raster.h` remplit les contours (quadratiques aplaties) comme les
  tracés, sans hinting comme Onyx. **Pas de crénage** dans `print_text`, ni dans sa mesure : `print_text` passe le texte
  à printd, qui le dispose (Onyx : FreeType, dans le kit). Letters et les autres applications qui disposent elles-mêmes
  (`print_font_data`, `print_glyph`) n'en dépendent pas.
* **Longueurs entières**, `print_image_mode` pour les drapeaux, `print_glyph` avec le style dans la police (les fonctions d'un kit
  ont 8 arguments au plus). Les images passent en zlib, pas en JPEG recodé (`PRINT_IMG_PHOTO` n'existe pas : Photos
  encode lui-même et appelle `print_image_jpeg`).
* **Le choix du papier d'après celui du document** n'est pas repris dans le dialogue.
* **Pas d'action sur la notification** (Onyx rouvrait les Imprimantes ou le PDF) : notifyd n'en a pas.
* **Protocoles** : comme Onyx, IPP en clair sur le port 631 (IPP Everywhere) et le PDF. Ni IPPS (une imprimante qui l'exige : le
  message le dit), ni le port brut 9100.
* netd : **multidiffusion en sortie** (`arp_lookup`, `ip_next_hop`) pour la recherche mDNS : avant, un paquet vers 224.0.0.251 partait
  à la passerelle.

## Sheet, Slides, le Lecteur PDF

Sheet dessine ses cellules en pixels avec `ui_text` : une page est dessinée à deux fois sa taille dans un canevas caché et va au
travail en image (pied de page en texte). Slides : une image de 2400 px par diapositive. Lecteur PDF : chaque page par MuPDF à
300 points par pouce ; vers l'imprimante PDF avec toutes les pages, le fichier est copié.

## Essais

```
python -I tools/nettest/ippsrv.py --port 8631 --dir build/ippjobs --log build/ipp.log
```

Dans l'applet : adresse `10.0.2.2:8631`, Ajouter, Essai. Le serveur écrit chaque document reçu et décode le PWG Raster en PNG
(`job<n>-p<k>.png`). `--pdf` : ne prend que le PDF ; `--busy N` : le travail reste en cours N interrogations (pour Annuler) ;
`--stop` : à l'arrêt ; `--none` : ni PWG ni PDF. Sous QEMU les grosses pages sont lentes (une image de 13 Mo : une minute avant
l'envoi) ; attendre.

Non essayé : une **vraie imprimante** ; la **recherche mDNS** de bout en bout (QEMU ne transmet pas la multidiffusion : seul
« aucune trouvée » l'a été). Le Wi-Fi du Pi ne reçoit peut-être pas les réponses ; elles sont en unicast (bit QU).
