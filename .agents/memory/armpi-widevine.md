---
name: armpi-widevine
description: "Au plan le 18/11 : Widevine dans Jet (Netflix, Disney+, Spotify web) par la couche Linux (process hôte du module de Google), EME dans WebKit, case des réglages ; après la couche Linux ; docs/etude/WIDEVINE.md, rien de fait"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026), à partir de LibreWolf (une case télécharge Widevine) et de l'envie d'avoir
Netflix et Disney+. Voie : process hôte en personnalité Linux qui charge `libwidevinecdm.so` (glibc) avec l'interface
CDM de Chromium, EME dans Jet par IPC, case « contenus protégés » qui télécharge le module chez Google (jamais dans
nos paquets), Jet vu comme Firefox/Chrome Linux. Attendu : L3, 720p pour Netflix/Disney+, fragile.

**How to apply:** après [[armpi-linux-compat]] (qui doit aussi charger un .so glibc dynamique ; son étude
docs/etude/LINUX-COMPAT.md n'existe pas encore). Détail : docs/etude/WIDEVINE.md. Voir [[armpi-webkit]], [[armpi-musique]].
