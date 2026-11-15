---
name: armpi-koton-ia
description: "Koton : « Composer avec l'IA » porté d'Onyx (15/11), /bin/llm sur OpenSSL, barre du haut placée à gauche des boutons de la fenêtre ; sur claude/x86-64-pc, publié (PC), posé sur D:, pas encore essayé dans l'OS"
metadata:
  type: project
---

15/11/2026, demande de l'utilisateur (sur le ZBook, Koton disait « SD:/bin/llm est celui d'Onyx ») : l'IA de Koton est portée.
`engine/ai*.cpp` d'Onyx tels quels (modèle Claude par défaut : claude-opus-5-5) ; `ui/ai_dialog.h` refait sur UIKit, suit
`llm` par `Dialog::onIdle` (ajouté dans view.h/.cpp) ; `src/bin/llm/main.cpp` = protocole d'Onyx + réseau POSIX/OpenSSL,
certificat vérifié. Paquet `llm` (avant [pc] / [armpi], qui prennent bin/), `app.koton` le demande ; koton 1.0.4, llm 1.0.0.

Barre du haut (`ui/chrome.h`) : le MASTER recouvrait le bouton d'IA (déplacé pour les boutons de la fenêtre sans lui) ;
maintenant placée depuis la droite avec `FRAME_CAP_W`, ce qui ne tient pas s'en va (DSP, libellé long de l'IA, bouton).

Publié le 15/11 (main 3547471, index du PC seul) : koton 1.0.4 (seul main remplacé dans l'archive publiée) et llm 1.0.0.
Les mots français sont dans res/lang/fr.txt, donc dans le paquet pc : ils n'arrivent qu'avec la prochaine publication de pc
(posés à la main sur D:). Le checkrepo de main refuse board = pc : le lancer avec le board accepté.

15/11 (suite, demande de l'utilisateur) : plusieurs clés par fournisseur, fenêtre « Clés d'API » (KeysDialog dans ui/ai_dialog.h :
fournisseur, nom, clé), liste déroulante des clés du fournisseur dans le dialogue d'IA ; settings.json `aiKeys`, l'ancien
`aiKey` repris. koton 1.0.5 publié (main 563034d, seul main remplacé). « Key » est traduit « Tonalité » (musique) : dire « API key ».

**How to apply:** llm essayé seulement compilé pour Linux contre un faux service local (simple, chunked, 503, téléchargement,
-o) ; TLS réel et le dialogue dans Korin restent à essayer. Le Pi : build-koton.sh construit llm s'il trouve build/openssl, pas
essayé. Voir [[armpi-x86-pc]], [[armpi-pocketqi]].
