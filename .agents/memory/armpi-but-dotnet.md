---
name: armpi-but-dotnet
description: "But ultime de l'utilisateur : compilateur et runtime .NET dans l'OS, GUI liée à UIKit, système à la WPF sur UIKit ; essais QEMU suffisent jusqu'au hello world C#"
metadata:
  node_type: memory
  type: project
  originSessionId: 6cbe2b78-9032-4b62-950f-92e82490eae8
  modified: 2026-10-31T18:18:31.693Z
---

Dit par l'utilisateur le 30/10/2026 : son but ultime est d'avoir **un compilateur et un runtime .NET dans l'OS**, **la GUI liée à UIKit**, et **un système « à la WPF » utilisant UIKit** : au moins un système qui lit un fichier décrivant la GUI d'une fenêtre, d'un user control, etc.

Moteur : CoreCLR de .NET 10.0.14 compilé par notre GCC (choix de l'utilisateur, clang en repli). **Hello world C# réussi le 31/10/2026 sous QEMU** ; état : docs/DOTNET.md, `tools/dotnet/`, sources dans `~/armpi-dotnet` (WSL).

`dlopen` peut attendre (31/10/2026) : l'utilisateur fait rarement de P/Invoke ; les DLL managées n'en ont pas besoin, les natives vont dans un hôte statique.

Souhaits ajoutés le 31/10/2026 : `.cs` construits par `pibuild` ; `/dotnet` comme équivalent du GAC ; préchargement des DLL (si possible et peu coûteux en mémoire) avec un **applet de `control`** qui liste les assemblys disponibles et une **case à cocher** par assembly.

Assemblys `System.xxKit` voulus (31/10/2026) : un par kit, entre les applications et les kits (UIKit, AudioKit et ceux à venir ; NetKit peut-être pas nécessaire).

`System.Windows.Forms` voulu (31/10/2026) : notre propre DLL, même API que WinForms, simple pont vers UIKit (c'est lui qui l'a proposé : décidé, ne pas repartir sur Mono ni sur le vrai WinForms).

Consigne du 31/10/2026 : **enchaîner automatiquement jusqu'au but final**, sans demander à chaque étape ; ne revenir vers lui que sur un vrai blocage.

Fusion de claude/perf-suite dans main le 31/10/2026 (bb78241) : `dllpreload` sur `file_pin`, `VM_COW` ; `VM_PRIVATE` et `filecache.c` retirés ; UIKit multi-fenêtres aussi dans main. Suite prévue : `.ui` compilés au build par un générateur source Roslyn (`-analyzer`, `UiLoader` gardé pour l'aperçu), puis un IDE en C# (éditeur, concepteur visuel, aperçu). L'hôte `/dotnet/dotnet` prend 9 s à charger sous QEMU : le mettre dans `preload` (proposé, pas encore accepté).

État antérieur au 31/10/2026 (QEMU, d5086e2) : Roslyn et `.cs` dans pibuild, System.Windows.Forms et fichiers `.ui`, System.<Kit>, /dotnet, préchargement des DLL (Réglages > .NET, `VM_PRIVATE`) marchent. Reste : fusion de la branche multi-fenêtres (décision de l'utilisateur), puis `Form.Show`, OnPaint, événements bruts, taille préférée, System.DocumentKit.

Décision du 31/10/2026 sur le partage `mmap` de `claude/perf-suite` : **ne garder en mémoire que les DLL cochées** ; un fichier partagé automatiquement ne survit pas au dernier process qui le projette.

Décisions du 31/10/2026 : **crossgen2 pas dans l'OS** (précompiler nos DLL n'a pas changé le total) ; `tools/dotnet/setup.sh` récupère SDK, runtime et crossgen2 si WSL est perdu, plutôt que de les mettre dans le dépôt ; **pas de lanceur** : AppKit voit `MZ` et lance `/dotnet/dotnet`, pas de serveur exec ; `pibuild` : `lib/<nom>` devient une DLL pour une app, fondu dedans pour bin/sys/svc. `TYPE=lib` proposé, pas encore accepté.

Sur le Pi (31/10/2026, essais de l'utilisateur, 837987a) : les essais semblent bons, `hello` démarre en environ 3 s sans préchargement ; les temps de QEMU exagèrent le coût de la carte, ne pas optimiser le démarrage sur eux.

WinForms complété le 31/10/2026 (QEMU) : `Form.Show` non modal, `OnPaint` et souris / clavier par `ui_view` (pas d'ajout à UIKit), `Graphics`, `AutoSize` ; souris brute seulement sur les vues, pas sur les widgets d'UIKit. Suite : `.ui` compilés, puis IDE.

Décision du 31/10/2026 : barre de menus **comme sur Mac** : titre de l'application et **un seul menu par application** (Elegant, `menu_owner`, `win_app`), pour les natifs aussi. Les dialogues ne changent pas le titre. Point ouvert QEMU seulement : `formsdemo` lancé par telnet resta invisible (Elegant le connaît) ; pas vu sur le Pi, ni par le terminal.

Studio (31/10/2026, branche claude/dotnet-ide) : **une seule fenêtre à panneaux**, aperçu du `.ui` en haut dans le Studio, propriétés dès la v1, sortie repliée en 1024×768 (décidé par l'utilisateur, le flottant est écarté). Générateur `.ui` : **C# (Roslyn), seul** ; la version C a été retirée sur décision de l'utilisateur (QEMU, `pibuild` uidemo : 95 s en C#, 80 s en C : 15 % ne valent pas la connaissance des types du programme). Port Windows du Studio voulu après coup (WinForms réel, compile pour ARMPI, pas de kits).

Studio et couche .NET (01/11/2026, QEMU seulement, non commité au moment de la note) : couleurs C# et XML dans UIKit (`UTA_CS`, `UTA_XML`, `uikit_lex.inc`, `CodeEditor.Language`), sortie repliable (`Panel2Collapsed`, F4), bibliothèques (`studio.txt`, `type = lib`, `pibuild lib`, `lib/<nom>`), `MenuStrip` complet et menus dans les `.ui`, modèle objet `Document`, `Spreadsheet`, `Presentation` dans `System.DocumentKit` (`tools/dotnet/kits/Office.cs`, `docdemo` 54 PASS). Non vu : fichiers produits ouverts dans Office, LibreOffice, letters ou sheet ; raccourcis dans une 2e fenêtre. Étude du binaire WinForms commun Windows/ARMPI : session winforms-portable. Essais QEMU : `ui_menu` garde le pointeur du texte, ne pas le libérer ; ports FTP de données 2100+ à rediriger.

**Why:** il veut développer en C# sur l'appareil, avec une interface déclarative au-dessus de UIKit.

**How to apply:** pour tout ce chantier jusqu'au hello world en C#, **les essais sous QEMU suffisent**, ne pas demander le Pi. Les liaisons C# appellent UIKit directement, sans pont ([[armpi-ports-sans-pont]]), et l'interface suit le thème du bureau ([[armpi-ports-theme]]). Voir [[armpi-chantier-perf]], [[armpi-but-libc]].
