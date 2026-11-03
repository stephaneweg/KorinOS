# Le premier démarrage

`src/sys/setup` (`SD0:/sys/setup`) : l'assistant d'une carte neuve, sur UIKit, construit par `tools/pkg/build.sh`.

| Étape | Quoi | Où c'est écrit |
|---|---|---|
| Bienvenue | la langue ; l'assistant repart dans cette langue | `etc/system.ini` : `lang` |
| Région et clavier | le pays, le fuseau, le clavier, pris tout de suite | `etc/options.txt` : `pays`, `fuseau`, `keyb` |
| Écran et thème | la résolution, essayée puis gardée ou rendue ; le thème | `etc/options.txt` : `ecran` ; `etc/theme.txt` |
| Wi-Fi | les réseaux autour, le mot de passe ; à passer si besoin | `etc/wifi.txt`, par NetKit |
| Logiciels | les paquets du dépôt, à cocher, installés depuis Internet | la base des paquets |
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
