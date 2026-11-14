---
name: armpi-idee-web
description: "Idée du 13/11 pour plus tard : serveur web avec PHP sur Korin (porter Apache si possible) et un outil de développement web (HTML, XML, JS, CSS) qui lance ce serveur dans le dossier du projet, à la WebMatrix"
metadata:
  type: project
---

Le 13/11/2026, l'utilisateur a noté pour plus tard deux outils :
1. **Un serveur web avec PHP** : voir si on peut **porter Apache httpd** (avec PHP).
2. **Un outil de développement web** (HTML, XML, JavaScript, CSS) qui peut **démarrer ce serveur web dans le dossier du projet**, « un peu comme WebMatrix » (l'ancien outil de Microsoft livré par Web Platform Installer).

Pistes notées le 13/11 (pas encore étudiées) : Apache a besoin d'APR/APR-util, PCRE2, OpenSSL (mod_ssl ; OpenSSL déjà construit pour le PC), d'un MPM adapté à libposix (prefork demande fork, event demande epoll : peut-être worker sur poll) ; PHP en mod_php ou en FastCGI (php-fpm), et `php -S` (serveur intégré) peut suffire pour l'outil de développement. L'outil pourrait être une app UIKit en C++ ou une extension du Studio Avalonia ([[armpi-ide-avalonia]]), avec aperçu dans Jet ([[armpi-webkit]]).

**Why:** faire du développement web sur Korin, sur le PC comme sur le Pi.

**How to apply:** placé le 13/11 juste avant Godot (après le GPU NVIDIA). Dépend de Jet pour l'aperçu, et du réseau (Wi-Fi du PC). Voir [[armpi-x86-pc]], [[armpi-posix-par-necessite]].

**Précisions du 13/11 (soir)** :
- **.NET aussi** : le serveur web et l'outil de développement devraient supporter .NET (sites ASP.NET Core, Kestrel ou derrière Apache), puisque Korin aura .NET ; donc PHP **et** .NET.
- **Design d'abord** : faire appel à un designer (étude de design, maquettes) pour un outil « vraiment léché, bien pensé, pro et facile à utiliser » — même exigence que la finition du Studio ([[armpi-ide-avalonia-finition]]).
- **Puis l'analyse technique** décide de la technologie de l'outil : **Avalonia ou C++ natif** (UIKit) ; pas décidé d'avance.

**14/11 : décisions** : serveur web **maison** (pas Apache : plus facile à intégrer à Korin et à l'éditeur), PHP et .NET ; l'éditeur s'appelle **WebStudio** ; serveur + WebStudio **avancés avant SDL** dans le plan du PC (après .NET et Vibe), plus juste avant Godot.
**14/11, précisions** : un **applet du panneau de configuration** pour le serveur : ajouter des **sites** comme dans IIS (dossier racine, **plusieurs bindings** : nom d'hôte, port, schéma). **D'abord sans PHP ni .NET**, mais une interface de gestionnaires prévue dès la création pour les brancher facilement. **WebStudio couvre la syntaxe PHP et C#** (sites PHP et ASP.NET).
**14/11, certificats** (dans l'applet, sur OpenSSL) : générer un auto-signé, créer un CSR (avec sa clé privée), intégrer le certificat reçu du fournisseur (et sa chaîne), l'associer aux bindings https.
**14/11, Let's Encrypt** : client ACME intégré au serveur (HTTP-01 servi par lui, renouvellement auto à 30 jours de la fin, staging pour les essais ; DNS-01/jokers plus tard).
**14/11 (après-midi) : WebStudio en C++ natif sur UIKit** (choix de l'utilisateur, pas Avalonia/.NET). Étude de design faite :
`docs/DESIGN-WEBSTUDIO.md`, maquettes https://claude.ai/artifact/JrC2yQS1RtJsw71MjNFeS5 (six écrans : espace de travail A
avec aperçu intégré recommandé, accueil, nouveau site, variante B Jet à côté, 800x600, applet Serveur web). **En attente de
validation** : questions du §7 (A ou B, SD1:/sites, rechargement, rail, couleurs du code en Noir et WARNING sur blanc, icône,
écran 1920x1080). Contrat demandé au serveur maison (§5.4) : instance privée 127.0.0.1 port libre, journal structuré,
mode dev avec rechargement, configuration des sites partagée avec l'applet. Sources des maquettes : seulement dans l'artifact.
**14/11, réponses à l'étude** : aperçu SÉPARÉ (révisé le même jour, remplace « Jet intégré ») : F5 lance le serveur
et ouvre Jet juste à côté (maquette 4), onglet du site réutilisé, rechargement par script injecté par le serveur (SSE) ;
à faire : service jet (ouvrir dans le Jet lancé), placement des fenêtres (UIKit ne sait pas déplacer sa fenêtre) ; sites dans SD1:/home/sites ; rechargement à l'enregistrement seulement ;
à gauche l'arbre seul (double-clic dossier = déplier/replier, double-clic fichier = ouvrir ou montrer son onglet), pas
de rail ni de plan (recherche dans le panneau du bas, Git par Site › Git…) ; icône violette </> ; ZBook 1920x1080.
UIKit : UKC_LABEL prend ACCENT_VIVID en thème sombre (uk_area.c + uikit_area.inc, pas compilé) ; texte en WARNING =
mélange avec TEXTE1 par ui_mix (convention, pas de jeton nouveau).
