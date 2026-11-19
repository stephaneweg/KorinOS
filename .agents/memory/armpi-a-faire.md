---
name: armpi-a-faire
description: "Notes de l'utilisateur à reprendre à une date donnée (tâches courtes signalées en passant)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-18
---

**FAIT le 18/11 — Jet, loupe de la barre d'adresse** (signalé le 14/11, oubliée le 17/11) : la cause était dans UIKit,
pas dans Jet : `ui_w_move` d'une barre d'outils laissait ses boutons (`ui_toolbar_add`, UT_TOOL) à leur place.
Désormais ils bougent du même décalage (`uk_tbar_shift`, uk_tabs.c) et `uk_adapt` ne les place plus lui-même. Les
apps qui placent leurs boutons (mail, irc, letters) le font après la barre : inchangées. Pas encore vérifié à
l'écran (Jet sur le ZBook, cardfile, help).

**EN COURS, 18/11 — boutons du touchpad du ZBook** : boutons sous le pavé corrigés dans hidrd.c (bits du doigt, docs/etude/TOUCHPAD-PC.md « Les boutons du ZBook ») ; le rapport 42 (dessus, milieu, PointStick) arrive bien au pilote : à revérifier sur le ZBook avec le nouveau pc. Signalé : le touchpad marche, pas les boutons autour (3 sous
le touchpad : COL03 ; 3 au-dessus : COL05, PointStick ELAN). À faire dans i2chid, détail : docs/etude/PC-PILOTES.md
§5 point 0.

**Signalé le 18/11 — icône de batterie** : il faut un symbole quand le chargeur est branché (pas seulement l'éclair en charge). Détail : docs/etude/BATTERIE-PC.md « Reste à faire ».

**FAIT le 18/11 (fbb7e301, pas encore essayé à l'écran) — Fichiers en deux vues** (et sélection multiple, glisser = copier, Maj : déplacer). Demandé : : un bouton de la barre d'outils de Fichiers (src/apps/files/main.c, port C de files.asm) partage la
fenêtre en **deux vues indépendantes** côte à côte, chacune avec son dossier, son chemin et sa sélection, pour
copier ou déplacer d'un dossier à l'autre (glisser de l'une à l'autre avec le fantôme du glisser ; Copier/Déplacer
vers l'autre vue ; F5/F6 à la Total Commander à voir). La vue active est marquée (couleur du thème) ; le panneau de
gauche et la barre de chemin suivent la vue active ; le bouton revient à une seule vue. Le choix est retenu
(réglages de Fichiers) et rouvert avec la session. Aide de Fichiers (help_fr.md, help_en.md) à compléter.

**18/11, décisions de l'utilisateur sur Fichiers en deux vues** (maquette docs/maquettes/fichiers-deux-vues.html) :
- **variante B** : un seul en-tête (flèches, fil d'Ariane, boutons) qui suit la vue active, un bandeau par vue (nom,
  chemin, place libre) ; **un seul panneau de gauche**, qui suit la vue active ;
- **séparation déplaçable** à la souris (retenue) ; **icônes/liste choisi pour chaque vue**, indépendamment ;
- **« Copier vers l'autre vue » directement**, sans confirmation, sauf si un fichier existant serait écrasé ;
- **glisser-déposer = copie** ; **Maj** pendant le glisser = déplacement (remplace la règle « même volume : déplacer ») ;
- **pas de F5/F6** pour copier/déplacer (F5 reste Actualiser) ;
- **sélection multiple** (fichiers et dossiers), en surbrillance : **Ctrl+clic** ajoute ou retire de la sélection,
  comme Windows ; **Maj+clic** sélectionne une plage, comme Windows (confirmé le 18/11) ; copier, déplacer, glisser,
  supprimer agissent sur toute la sélection.

**Au plan le 18/11 — Réglages, tous les volets en modules** : chaque volet devient un `/sys/applets/<nom>.so` comme
Serveur web, SMB et Logiciels, livré par le paquet de son domaine ; Réglages n'est plus qu'un hôte. Ordre et
catégories par le contrat de control.h ; Réglages en hôte sur le Pi aussi. Détail : src/apps/control/README.md.

**Bug ouvert (19/11) : Wi-Fi et pavé tactile (ZBook)** : dans l'installateur, « Se connecter » cliqué au pavé tactile
échoue (« Mot de passe refusé ») ; Tab puis Espace, ou Entrée, connecte. Même code des deux côtés : c'est l'activité du
pavé (i2chid, IRQ IOAPIC, attente active de dw.c) pendant la connexion d'iwl (MSI, service(10), EAPOL en 6 s).
Cause pas trouvée en lisant le code ; il faut les lignes `iwl :` du journal d'un échec. Mis de côté par l'utilisateur.
