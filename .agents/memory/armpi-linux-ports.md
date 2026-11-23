---
name: armpi-linux-ports
description: "23/11/2026 : Koton et Arabesque portés sous Linux (Fedora 45) dans linux-ports/ : UIKit refait sur GTK 4, AudioKit sur FluidSynth, sources des apps compilées telles quelles ; binaires dans linux-ports/bin"
metadata:
  type: project
---

Demande de l'utilisateur (23/11) : Koton (« KotonStudio ») et Arabesque sous Linux, interface GTK 4, son FluidSynth
(FluidR3_GM par défaut, autre .sf2 au choix), moteur intact, binaires commités dans le dépôt.

Choix : pas de réécriture de l'interface ; `linux-ports/common/uikit_gtk.cpp` refait les ~100 `ui_*` utilisées
(widgets GTK placés au pixel dans un widget maison KSurf qui montre ui.surf en fond ; vues ui_view = KSurf à pixels ;
file d'événements rendue par ui_event ; dialogues GTK en boucle imbriquée), `audiokit_fluid.cpp` les `ak_*`
(un fluid_synth par synthé, sortie par new_fluid_audio_driver2, MP3 par dlopen de libmp3lame), `korin_paths.cpp`
enveloppe open/fopen/opendir/readdir/mkdir/stat/posix_spawn (-Wl,--wrap) pour HOME:/ (~/Korin), /apps/<app>.app,
/res/soundfonts. Les main.cpp de Korin sont compilés avec -Dmain=kport_app_main ; linux_main.cpp ajoute
`arabesque --render x.arb y.mid [--wav --mp3 --fms]` et `koton --export-wav`.

Pièges : gtk_init met LC_NUMERIC français (.ifs lus faux) -> setlocale (LC_NUMERIC, "C") ; SCHED_FIFO refusé (EPERM)
-> korin_thread.cpp relance sans (Koton et ses greffons) ; readdir non trié sous Linux -> trié ; pas de
gtk_window_set_default_size (compte le titre) ; un ui_slider créé 10x24 puis élargi doit changer d'orientation.

Vérifié : 20 préréglages, même .mid à chaque fois et en -O0 (FMA : « nuit » diffère) ; kptest 107/107 ; captures par
KORIN_SCRIPT. Pas comparé à un .mid de Korin même ; souris réelle, dialogues de fichiers et IA pas essayés à la main.

Piège (23/11, « Composer avec l'IA » figeait Koton, GTK : « ne répond pas ») : build/gen/uikit_abi.h (abi2c avec
armpi.inc) définit O_TRUNC 8, O_APPEND 16, O_NONBLOCK 32 de Korin, inclus avant <fcntl.h> : ils remplaçaient ceux de
Linux. Le tube de llm restait bloquant (read dans LlmRun::drain) et O_TRUNC ne tronquait pas (fichiers réécrits plus
courts : fin de l'ancien contenu gardée). Le Makefile retire ces trois blocs après abi2c. Les captures shot2 de
KORIN_SCRIPT peuvent être périmées (le bouton masqué y restait visible) : vérifier l'état par gtk_widget_get_visible.
Rpm : RELEASE dans packaging/make-rpms.py (2 depuis ce correctif).
