# Mode poche : décisions et contrat entre les pièces

Maquettes validées par l'utilisateur le 5/11/2026 : https://claude.ai/artifact/1qG21UencFn3UUzGWxPx6y
(étude : `docs/DESIGN-ETUDES-ONYX.md`, section 4). Ce fichier fixe ce que les pièces du mode poche
partagent, pour qu'elles se construisent séparément puis s'emboîtent.

## Décisions de l'utilisateur

1. Accueil : **variante A**, le tableau du jour : grande heure et date, recherche, trois cartes « Aujourd'hui »
   (prochain rendez-vous de l'agenda, météo de PocketQI, musique en cours avec pause), rangée « Récentes »,
   « Toutes les apps » qui ouvre le lanceur à rail (celui du panel) en second niveau, bande « Ouvertes » de 64 px
   en bas (pastilles icône 24 + nom + ×, sans vignette).
2. Barre du haut en poche : **36 px**.
3. Densité tactile : contrôles et lignes de **36 px** (troisième ligne de `uk_mt_tab`) ; les éléments de la
   coquille sont plus grands (tuiles 60, pastilles 44, cellules d'icônes 96).
4. **Boutons d'abord**, pas de gestes de bord (un glissé du bord droit pourra venir plus tard, désactivable).
5. Notifications **dans le volet des réglages rapides** ; toast pleine largeur sous la barre, 4 s ; point d'accent
   sur l'heure quand il y en a de non lues.
6. **Vignettes** dans le commutateur (copies réduites une fois à l'ouverture), pas dans la bande de l'accueil.
7. Fenêtres de taille fixe (Calculatrice, etc.) : **plein écran, contrôles adaptatifs** (pas de carte centrée).
   Les dialogues `WF_MODAL` gardent leur taille, centrés sur un voile `TEXTE1` à 30 %.
8. Glyphe ‒ du cadre : **ramène à l'accueil** ; × ferme ; agrandir disparaît ; `FRAME_CAP_W` inchangé.
9. Changer de mode : **relancer la session** d'abord (apps fermées) ; à chaud plus tard. Sortie de secours au
   démarrage (touche Maj maintenue, ou `SD0:/options.txt`) qui force le bureau.
10. Résolution : **800 x 600** gardée.

## Contrat

* Le mode : `shell = poche` (ou `desktop`, défaut) dans `SD0:/etc/system.ini`, lu au démarrage par Elegant.
  En poche, la session est `SD0:/etc/session.poche` au lieu de `SD0:/etc/session`.
* Elegant publie sur le bus `korin.shell.mode` = `desktop` ou `poche` (les programmes peuvent aussi lire
  `system.ini`).
* Commandes de la coquille, publiées sur le bus par Elegant (touches système) ou par la barre de menus (boutons) ;
  la valeur est libre (`1`) sauf mention :
  * `korin.shell.home` : montrer l'accueil (Super, losange ◆, glyphe ‒). Un second Super revient à l'app.
  * `korin.shell.switch` : ouvrir le commutateur (Alt+Tab, Super+Tab, compteur ‹n›).
  * `korin.shell.quick` : ouvrir ou fermer les réglages rapides (Super+N, l'heure).
  * `korin.shell.sleep` : mettre en repos (Super+L, touche Sleep, tuile Repos) — l'écran de repos viendra de
    l'étude 3 ; en attendant, publié seulement.
* Programmes de la coquille (session poche) : `SD0:/sys/home` (accueil), `SD0:/sys/quick` (réglages rapides),
  le commutateur dans Elegant ou `SD0:/sys/switcher` (au choix de qui le fait), la barre de menus
  `SD0:/sys/menubar --poche`, `notifyd`.
* Drapeau de fenêtre **`WF_HOME = 16384`** (dans `src/inc/ws_ext.inc`) : la fenêtre de l'accueil. En poche,
  Elegant la tient sous toutes les fenêtres normales, en plein écran sous la barre, toujours là ; elle
  n'apparaît pas dans le compteur ni dans le commutateur.
* Les numéros d'ops `WS_OP_*` nouveaux partent de 37, en fin de liste.
