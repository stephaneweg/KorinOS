---
name: armpi-linux-ports
description: "23/11/2026 : Koton, Arabesque et Budget (banque comprise) portés sous Linux (Fedora 45) dans linux-ports/ : UIKit sur GTK 4, AudioKit sur FluidSynth, coffre sur libsecret, sources des apps compilées telles quelles ; binaires dans linux-ports/bin ; Budget sur la branche claude/linux-budget"
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
Rpm : RELEASE dans packaging/make-rpms.py (2 depuis ce correctif, 3 depuis Budget).

Budget (23/11, branche claude/linux-budget, pas poussée) : bin/budget, budget/linux_main.cpp, paquet korin-budget
(version [pc]). Ajouts à common/ : korin_sys.cpp (armpi_syscall -> kport_syscall : SYS_TIME_MS, SYS_CLOCK_GET ;
avant, -ENOSYS pour tout : l'horloge de Budget aurait été fausse), korin_vault.cpp (KV_SVC_OPEN/CALL de keyvault.h
sur libsecret, schéma org.korin.KeyVault, attributs app/name/home ; installé par Budget seul, Koton inchangé),
korin_paths : Jet (/apps/jet.app/main, SD0:...) -> xdg-open / gio open / $KORIN_BROWSER (stat et posix_spawn),
kport_open_external (ui_assoc_open) ; uikit_gtk : ui_dialog_button/run/close/cancel/timer (boucle de uk_dialog.c),
ui_metric (échelle « petite »), ui_text_prefix, ui_textbox_select, ui_w_get_focus, ui_lang, ui_tr_load,
kport_tr_add (mots dits autrement sous Linux), popup 0x02 coché ; script : key2, chars2, click2 (,3 droit), press2,
dump/dump2 (état réel des widgets). Budget ne lie pas audiokit (kport_audio_reload_soundfont factice dans son main).
Pièges : libsecret-devel absent ici -> en-têtes pris par « dnf download libsecret-devel » dans build/libsecret-devel,
.pc réécrit (prefix, -l:libsecret-1.so.0, sans Requires.private gnutls), PKG_CONFIG_PATH=build/libsecret-devel/pc
make ; sans cela le Makefile construit sans coffre. Les clics de script sont en coordonnées du contenu (sans la barre
de titre de la capture). Essais : faux serveur Enable Banking et faux navigateur (curl -L) dans build/fakebank (non
commités), KORIN_HOME d'essai ; secrets d'essai retirés par « budget --vault-forget ».
