---
name: korin-widget
description: Faire un widget du bureau de Korin OS, comme ceux de l'Agenda et des Notes épinglées - une carte posée sur le fond d'écran, sous toutes les fenêtres (fenêtre WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST | WF_ALPHA), sa place, son aspect (verre translucide, jetons du thème), sa boucle (relecture lente, réseau dans un thread), le clic qui ouvre l'app, le lancement à l'ouverture de la session (HOME:/etc/startup.txt). À utiliser dès qu'on demande un widget, une applet ou une carte sur le bureau (météo, horloge, cours, état du système...).
user-invocable: true
---

# Faire un widget du bureau

Un widget est un **petit programme** qui montre une carte d'information **sur le fond d'écran, sous toutes les
fenêtres** : l'Agenda (les prochains rendez-vous, en haut à gauche) et les Notes épinglées (en haut à droite) en sont
deux. Il ne prend jamais le clavier, n'apparaît ni dans le dock ni dans la liste des fenêtres, et un clic dessus
ouvre l'app qui va avec. À lire avec `korin-app` (projet, `app.txt`, `pibuild`) ; le dessin UIKit : `korin-graphics`.
Exemples dans `examples/` : `agenda.c` et `stickies.c` (ceux du système : ils sont écrits sur le runtime du système,
`korinrt`, mais leurs `ui_*` sont ceux de `uikit.h`).

## 1. Une app, deux rôles

Le plus simple pour l'utilisateur : **un seul programme** avec deux rôles.

* `main` : l'app normale, dans une fenêtre (`WF_RESIZABLE`) : réglages du widget (la ville d'une météo, le compte
  suivi...), détail, actualisation.
* `main --widget` : la carte du bureau. Elle **ne fait que lire** ce que l'app écrit (un fichier de réglages dans
  `HOME:/.config/<nom>.ini`) et le relit de temps en temps : l'app et le widget ne se parlent pas autrement.

**Le widget sous son propre nom de process.** Le noyau nomme le process de `/apps/<nom>.app/main` d'après son
dossier : `<nom>`. Or le panel tient une app pour **ouverte** dès qu'un process porte son nom (il montre son icône
comme celle d'une app lancée), et le gestionnaire de paquets **refuse de mettre à jour** une app qui tourne. Un widget
lancé sous le nom de l'app ferait donc les deux. Le rôle widget se relance une fois, par `execv`, sous un autre nom
(`korin_exec_name` de la libc, déclaré à la main), comme le fait Météo (`src/apps/weather/main.c`) :

```c
#include <unistd.h>
void korin_exec_name (const char *name);	/* libc de Korin : le nom du process après le prochain execv */

int main (int argc, char **argv)
{
	const char *role = argc > 1 ? argv[1] : "";
	if (!strcmp (role, "--widget"))
	{					/* même process, sous le nom « monwidget-widget » */
		char *args[] = { "/apps/monwidget.app/main", "--widget-run", 0 };
		korin_exec_name ("monwidget-widget");
		execv (args[0], args);
		korin_exec_name (0);		/* (échec : on continue sous le nom de l'app) */
	}
	if (!uikit_open (&ui)) return 1;
	int widget = !strcmp (role, "--widget") || !strcmp (role, "--widget-run");
	return widget ? widget_main () : app_main ();
}
```

* La ligne de démarrage et l'app gardent `main --widget` ; `--widget-run` ne sert qu'à ce relancement.
* Le nom fait 31 caractères au plus. `sysstat` montre alors `monwidget-widget`, et `kill` le vise sans toucher l'app.
* Une mise à jour passe pendant que le widget tourne (le process garde son ancienne image en mémoire). Pour qu'il
  prenne la nouvelle version sans attendre la session suivante : comparer de temps en temps la date et la taille de
  `/apps/<nom>.app/main` (`stat`) à celles du départ, et, quand elles ont changé et sont restées pareilles d'une
  lecture à l'autre, lancer `main --widget` puis s'arrêter. Le nouveau attend que l'ancien soit parti (un seul widget
  à la fois : chercher un autre process de ce nom dans `SYS_PROCLIST`, comme Météo).

## 2. La fenêtre du widget

```c
#define CARD_W	344				/* la largeur de l'Agenda : les cartes de gauche s'alignent */
#define CARD_H	186
#define W_X	16				/* colonne de gauche : APPLET_X */
#define W_Y	(44 + 190 + 12)			/* sous l'Agenda (y 44, hauteur 190), 12 px d'écart */

if (ui_window_ex (x, y, CARD_W, CARD_H, (long) ui_tr ((long) "Mon widget"),
		  WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST | WF_ALPHA) <= 0)
	return 1;
```

| Drapeau | Effet |
|---|---|
| `WF_FRAMELESS` | pas de cadre ni de barre de titre |
| `WF_BACK` | au fond : sur le fond d'écran, sous toutes les fenêtres |
| `WF_NOFOCUS` | ne prend jamais le clavier (un clic ne vole pas le focus) |
| `WF_NOLIST` | absent du dock, d'Alt+Tab et de la liste des fenêtres |
| `WF_ALPHA` | l'octet haut de chaque pixel est sa **transparence** : `0x00` opaque, `0xFF` invisible |

**La place.** Le haut de l'écran a la barre de menus (contenu à partir de y = 44), le bas le dock (garder 64 px).
L'Agenda occupe `(16, 44)`, 344 × 190 ; les Notes épinglées la colonne de droite (240 px de large, de y = 44
jusqu'au dock). Un widget neuf se met **dans la colonne de gauche, sous l'Agenda** (`W_X`, `W_Y` ci-dessus), ou
plus bas sous un autre ; jamais par-dessus les deux du système. La taille de l'écran : `ui_screen (scr)` (`scr[0]`,
`scr[1]`). Laisser l'utilisateur la changer : `widget_x`, `widget_y` dans le `.ini` de l'app (lus au démarrage).

## 3. L'aspect : du verre sur le fond d'écran

```c
#define CLEAR 0xFF000000u			/* tout transparent */
#define GLASS 0x28000000u			/* le fond de carte laisse voir un peu le bureau */

static void paint (void)
{
	const unsigned *tj = (const unsigned *) ui_tokens ();
	unsigned glass = (tj[TJ_SURFACE] & 0xFFFFFF) | GLASS;
	ui_fill (0, 0, ui.w, ui.h, CLEAR);			/* d'abord tout transparent */
	ui_rbox (0, 0, ui.w, ui.h, 10, glass, glass);		/* la carte, coins de 10 px */
	ui_text ((long) ui_tr ((long) "Titre"), 14, 23, tj[TJ_TEXT1], 14, (long) ui.font_b, 0);
	ui_fill (12, 34, ui.w - 24, 1, tj[TJ_LINE]);		/* le trait sous le titre (bandeau de 34 px) */
	/* les lignes : texte de 13 px (ui_text_fit pour couper), TJ_TEXT2 pour le secondaire */
	ui_update_all ();
}
```

* **Que les jetons du thème** (`TJ_SURFACE`, `TJ_TEXT1`, `TJ_TEXT2`, `TJ_TEXT3`, `TJ_LINE`, `TJ_ACCENT`,
  `TJ_WARNING`, `TJ_DANGER`...) : jamais une couleur en dur pour l'interface. Sur `WEV_THEME`, relire
  `ui_tokens ()` et **tout redessiner** (Givre clair, Onyx Noir sombre).
* Dans un tampon `WF_ALPHA`, un dessin **écrit** les pixels, il ne se mélange pas à ce qui est dessous dans le
  tampon : poser le fond de la carte d'abord, le texte ensuite ; un anneau translucide se dessine de l'extérieur
  vers l'intérieur. Le mélange avec le bureau, c'est Elegant qui le fait.
* Le gabarit des widgets du système : bandeau de titre de 34 px (titre 14 px gras à x = 14, base 23), lignes de 24 px,
  marges de 12 px, texte 13 px. Une icône ou un petit dessin à gauche du titre est bienvenu.
* Lisible sur **tous les fonds d'écran** : texte sur la carte, jamais directement sur le bureau.

## 4. La boucle : lente et sobre

```c
static int widget_main (void)
{
	/* ... ouvrir la fenêtre (§2), lire les réglages, paint () ... */
	time_t last = 0;
	for (;;)
	{
		long t = ui_event (1000);		/* réveil au moins chaque seconde : rien ne tourne à vide */
		if (t == WEV_CLOSE) break;
		if (t == WEV_THEME) paint ();
		else if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1) open_app ();
		time_t now = time (0);
		if (now - last >= 60) { last = now; if (reload ()) paint (); }	/* ne redessiner que si ça a changé */
	}
	return 0;
}
```

* Relire **lentement** : une minute pour une horloge ou un agenda, 15 minutes pour la météo ; les réglages de l'app
  toutes les quelques secondes au plus. Ne redessiner que ce qui a changé.
* **Le réseau dans un thread** (`pthread_create`), jamais dans la boucle : la carte affiche « Chargement… » puis
  les données ; sans réseau, garder les dernières données et réessayer plus tard (une minute), sans message
  d'erreur criard. Le guide NetKit : `korin/reference/netkit_en.md`.
* **Le clic** ouvre l'app (le rôle fenêtre) :

```c
#include <spawn.h>
extern char **environ;
static void open_app (void)
{
	char *argv[] = { "/apps/monwidget.app/main", 0 };
	pid_t pid;
	posix_spawn (&pid, argv[0], 0, 0, argv, environ);
}
```

* Une carte cliquable par endroits (une ligne de l'Agenda ouvre ce jour) : garder les rectangles des lignes et
  comparer `EV_X`, `EV_Y` au clic.

## 5. Le lancer à l'ouverture de la session

Les widgets du système sont dans `/etc/session` (lancés par Elegant). Un widget d'utilisateur va dans **la liste
personnelle** `HOME:/etc/startup.txt` (une ligne par programme : `chemin [arguments]`, `#` commente ; init2 la lance
une fois la session ouverte) :

```
/apps/monwidget.app/main --widget
```

* Si `HOME:/etc/startup.txt` n'existe pas encore, partir d'une copie de `/etc/startup.txt` s'il y en a un (la liste
  personnelle **remplace** celle du système).
* **Demander à l'utilisateur** avant de l'ajouter. Il le voit ensuite dans Réglages, Démarrage (Session), où il peut
  le passer en Manuel ou Désactivé.
* L'essayer tout de suite, sans attendre la prochaine session : `(/apps/monwidget.app/main --widget >/dev/null 2>&1 &)`.
  Un seul exemplaire à la fois : le relancer, c'est d'abord arrêter l'ancien (`sysstat | grep monwidget-widget`, `kill`).

## 6. Vérifier avant de rendre

* `pibuild` réussi, le widget lancé, `screenshot /tmp/s.bmp` : la carte est au bon endroit, ne recouvre ni l'Agenda
  ni les Notes, se lit sur le fond d'écran, en Givre et en Onyx Noir (changer de thème : Réglages, Thème).
* Une fenêtre ouverte passe **par-dessus** le widget ; un clic sur le widget ouvre l'app sans voler le focus.
* Le widget seul tourne : **le panel ne montre pas l'app comme ouverte**, et le gestionnaire de paquets la met à jour.
* `app.txt` : dire dans `help` que le widget existe et comment l'ajouter au démarrage.
