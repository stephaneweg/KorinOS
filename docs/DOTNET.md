# .NET sous ARMPI

**CoreCLR de .NET 10.0.14** tourne sous ARMPI, avec son JIT, sa bibliothèque et le compilateur C# : on
écrit, compile et lance du C# dans l'OS, avec une GUI sur UIKit. C'est du code porté, précompilé sur le PC,
**hors du selfhost**. But et suite : section .NET de [FEUILLE-DE-ROUTE.md](FEUILLE-DE-ROUTE.md).

ARMPI se fait passer pour **Linux arm64** : les bibliothèques C# de `linux-arm64` servent telles
quelles, seul le natif est refait, par la chaîne d'Onyx (GCC 14.2, newlib, libposix).

| Où | Quoi |
|---|---|
| `tools/dotnet/build.sh` | construit l'hôte statique `singlefilehost` (WSL) |
| `tools/dotnet/toolchain.cmake` | notre GCC pour CMake, `-D__linux__` |
| `tools/dotnet/armpi.patch` | les retouches de `dotnet/runtime` |
| `tools/dotnet/compat/` | les noms de Linux que libposix n'a pas à porter (`mntent.h`, `netinet/in.h`...) |
| `tools/dotnet/hello.sh` | publie `hello` ou `check` dans `build/dotnet/sdroot/bin/`, recopié par `build.ps1` |
| `tools/dotnet/hello/`, `check/` | le premier programme, l'essai du framework |
| `tools/dotnet/stage.sh`, `kits.sh` | remplit `/dotnet`, compile nos assemblys |
| `tools/dotnet/kits/`, `forms/`, `app/`, `dllpreload/` | liaisons des kits, WinForms, `main` des applications, préchargement |
| `tools/dotnet/cshello/`, `libuse/`, `docdemo/`, `formsdemo/` | les essais de `pibuild`, de DocumentKit et de WinForms |
| `~/armpi-dotnet` (WSL) | `runtime/` (étiquette `v10.0.14`), `sdk/` (SDK 10.0.403), `obj/`, hors de git |

```sh
sh tools/posix/build.sh
sh tools/dotnet/build.sh
sh tools/dotnet/hello.sh check
sh tools/dotnet/stage.sh
sh tools/dotnet/kits.sh
```

## Comment un programme tourne

`singlefilehost` lie **tout en un seul exécutable statique** (10,6 Mo) : l'hôte, CoreCLR, le JIT, le
ramasse-miettes, `System.Native`. Il remplace celui de Microsoft dans le paquet NuGet, puis
`dotnet publish` y ajoute le C# : un ELF d'ARMPI ordinaire (`ET_EXEC` à `0x40_0000_0000`). Rien ne passe
par `dlopen`.

`dotnethello` et `dotnetcheck` sont découpés par `PublishTrimmed` (12,7 et 13,7 Mo, contre 80 Mo).

Il faut pour l'instant deux réglages à l'environnement :

```sh
export DOTNET_EnableWriteXorExecute=0 DOTNET_GCHeapHardLimit=20000000
```

* **W^X coupé** : il mappe deux fois le même code ; pas essayé sur `SHM_MAP`.
* **Limite du tas** (hexadécimal, 512 Mo ici) : sans elle, le ramasse-miettes veut réserver plus que
  les 64 Go de l'arène de `VM_MAP`.

## Le runtime partagé : `/dotnet`

`sh tools/dotnet/stage.sh` remplit `build/dotnet/sdroot/dotnet/`, copié par `build.ps1` sur `SD0:/dotnet`,
vu comme `/dotnet` (`etc/paths.txt`) ; c'est l'équivalent du GAC, 118 Mo :

| Dans `/dotnet` | Quoi |
|---|---|
| `dotnet` | l'hôte statique (`dotnet /chemin/programme.dll`) |
| `*.dll` | le framework de `linux-arm64`, inchangé, et nos assemblys |
| `roslyn/` | le compilateur C# (`csc.dll`, archive arm64 du SDK) |
| `ref/`, `csc.rsp` | les assemblys de référence, les options que `pibuild` donne à Roslyn |
| `dllpreload` | le préchargement (plus bas) |
| `Newtonsoft.Json.dll` | 13.0.4 (`lib/net6.0` du paquet NuGet, licence MIT à côté), visible de tous les programmes et de `pibuild` ; essai : `tools/dotnet/jsondemo` |

`sh tools/dotnet/kits.sh` compile sur le PC, avec Roslyn, ce qui est à nous (`tools/dotnet/kits/`,
`forms/`) ; `tools/kits/abi2cs.py` génère les liaisons depuis les `.abi` :

| Assembly | Classe | Pour |
|---|---|---|
| `System.Kits` | `Kit` | ouvre un kit, trouve une fonction, appelle (`armpi_call` : plusieurs résultats) ; chaînes UTF-8 |
| `System.UIKit` | `UI` | les `ui_*`, les jetons du thème |
| `System.AudioKit` | `AK` | les `ak_*` |
| `System.NetKit`, `System.GpuKit`, `System.MediaKit` | `NK`, `GK`, `MK` | les autres kits |
| `System.DocumentKit` | `DK`, `Doc`, `Document`, `Spreadsheet`, `Presentation` | les `dk_*`, `Doc` en types C# (`tools/dotnet/kits/Document.cs`) : format d'un document, relations OOXML, dates d'Excel ; le modèle objet (`Office.cs`, plus bas). Essai : `tools/dotnet/docdemo` |
| `System.Windows.Forms`, `System.Windows.Forms.Primitives`, `System.Drawing.Common` | | ci-dessous ; mêmes noms, version et clé publique qu'à Windows ([WINFORMS-PORTABLE.md](WINFORMS-PORTABLE.md)) |
| `System.3D` | `Viewport3D` | la 3D sur le GPU, espace de noms `System.Windows.Media.Media3D` ([VIEWPORT3D.md](VIEWPORT3D.md)) |

Une application ne fait pas de `kit_open` : le kit s'ouvre au premier appel.

## Compiler en C# dans ARMPI

`pibuild dossier nom` sur un dossier qui contient des `.cs` ([GCC.md](GCC.md)) : Roslyn donne
une DLL installée comme un programme, `/apps/nom.app/main` ou `/bin/nom`, et les `.ui` sont copiés à
côté. Il n'y a pas de lanceur : au lancement, AppKit voit `MZ` en tête du fichier (`f_spawn_file`) et lance
`/dotnet/dotnet` sous le nom du programme, avec le chemin de la DLL en `argv[1]`. Essai : `tools/dotnet/cshello`, copié sur `/dotnet/test/cshello` (appelle AudioKit
et LINQ). Sous QEMU : 141 s de compilation.

`pibuild lib dossier nom` construit une bibliothèque seule : `/dotnet/nom.dll`, que tout programme charge à la demande, et
sa référence dans `/dotnet/ref/user/`, que `pibuild` passe à chaque compilation suivante. `lib/` y est fondu. Pour retirer
une bibliothèque, effacer ces deux fichiers.

## Projets .csproj

Un dossier qui contient un `.csproj` se construit avec `pibuild dossier nom`, et c'est le projet qui décide : le même fichier s'ouvre dans Visual Studio. MSBuild ne tourne pas dans l'OS : `tools/dotnet/project` (`Armpi.Project.CsProject`) lit un sous-ensemble, et `tools/dotnet/csbuild` construit, Roslyn dans son propre process.

| Dans le `.csproj` | Effet |
|---|---|
| `OutputType` `Exe`, `WinExe` | un programme, `/apps/nom.app/main`, avec `app.txt` et `data/` |
| `OutputType` `Library` ou absent | `/dotnet/AssemblyName.dll` et sa référence dans `/dotnet/ref/user/` |
| `ArmpiType` `bin`, `sys`, `svc` | propriété à nous, ignorée par Visual Studio : autre place d'installation |
| `ProjectReference` | construit d'abord ; sa DLL va à côté du programme |
| `Compile` `Include`, `Exclude`, `Remove`, motifs, `**/*.cs` par défaut hors `bin` et `obj` | les sources |
| `Reference` | `HintPath`, sinon `/dotnet/ref` |
| `PackageReference` | pas de NuGet : `/dotnet/ref/Id.dll` ou `/dotnet/ref/packages/Id/`, sinon erreur |
| `ImplicitUsings`, `Nullable`, `LangVersion`, `DefineConstants`, `StartupObject`, `NoWarn`, `TreatWarningsAsErrors` | comme le SDK |
| `Condition` | `==`, `!=`, `And`, `Or`, `!`, `Exists`, parenthèses ; une autre forme est tenue pour fausse, avec un avertissement |
| `Link`, `LinkBase`, `Compile Update`, `AvaloniaXaml Update` | la place du fichier dans l'arbre du projet : un dossier virtuel, le fichier ne bouge pas sur le disque. Un `.axaml` déplacé par `Link` change aussi son adresse `avares://`. Le Studio l'écrit par Ranger… |
| `Folder` | un dossier vide dans l'arbre ; le Studio le crée par Dossier, comme Visual Studio : sur le disque et dans le `.csproj` |
| motifs hors du dossier, `..\Commun\**\*.cs` | les sources d'un dossier voisin |
| `Directory.Build.props` | le plus proche en remontant |
| `Import`, `Target`, `UsingTask` | ignorés, avec un avertissement |

Un projet qui référence le paquet `Avalonia` est compilé avec son XAML ([AVALONIA.md](AVALONIA.md)). Les `.ui` sont compilés comme sans `.csproj`. Un projet à jour n'est pas recompilé (`/tmp/csbuild`).

`python -I tools/dotnet/csbuild/test/parity.py sortie projet.csproj...`, sur le PC, compare ce lecteur au vrai MSBuild (type, nom, framework, sources). Essai dans l'OS : `pibuild /dotnet/test/multi/app multiapp2`, puis `/apps/multiapp2.app/main`.

## Documents de bureau

`System.DocumentKit` lit et modifie les documents sans ouvrir d'application. Ce qui n'est pas touché reste tel quel
dans le fichier (images, styles, mise en page), et le format ne change pas à l'enregistrement.

```csharp
var doc = Document.Open ("/home/rapport.docx");
foreach (var p in doc.Paragraphs) Console.WriteLine (p.Style + " : " + p.Text);
doc.Add ("Une ligne de plus");
doc.Save ();

var book = Spreadsheet.Open ("/home/compta.xlsx");
book.Sheets[0].Cells["B2"].Value = 42;
book.Save ("/home/compta2.xlsx");

var deck = Presentation.Open ("/home/cours.pptx");
deck.Slides[0].TitleParagraph.Text = "Nouveau titre";
deck.Save ();
```

| Classe | Formats | Création |
|---|---|---|
| `Document` : `Paragraphs`, `Text`, `Add`, `Remove`, `Save` | `.docx`, `.odt` | `Document.Create (DK.DKK_ODT)` |
| `Spreadsheet` : `Sheets`, `AddSheet`, `Sheet.Cells["B2"]` ou `[ligne, colonne]`, `Sheet.Used`, `Cell.Value`, `Cell.Formula` | `.xlsx`, `.ods`, `.csv` | `Spreadsheet.Create (DK.DKK_ODS)` |
| `Presentation` : `Slides`, `Slide.Title`, `Slide.Paragraphs` | `.pptx`, `.odp` | non : une présentation vide demande un masque |

Limites : `Paragraph.Text =` remplace les morceaux du paragraphe par un seul, avec la mise en forme du premier. `Cell.Value`
est `null`, `string`, `double` ou `bool` : une date reste son numéro de série (`Doc.FromExcel`). Écrire une valeur enlève la
formule de la cellule, et rien ne recalcule : la valeur d'une formule est celle que le fichier a gardée. Une feuille `.ods` modifiée
est récrite ligne à ligne : ses cellules gardent style et formule, mais ses lignes perdent leur hauteur. `Sheet.Cells` crée
une cellule vide quand on la demande : `Sheet.Used` ne donne que celles qui ont une valeur.

## WinForms sur UIKit

`System.Windows.Forms` (`tools/dotnet/forms/`) a l'API de WinForms, écrite pour ARMPI : chaque contrôle est
un widget d'UIKit, donc le thème du bureau s'applique. Pas de code de Mono ni de WinForms.

Un binaire compilé contre le vrai WinForms tourne ici s'il reste dans l'API commune, et un binaire de `pibuild` tourne sous Windows s'il n'utilise que cette API : [WINFORMS-PORTABLE.md](WINFORMS-PORTABLE.md). `tools/dotnet/portable/apidiff` vérifie un binaire et mesure l'écart. Tout membre public ajouté ici doit exister à Windows, sur le même type, ou aller dans une extension.

* **Contrôles** : `Button`, `Label`, `TextBox`, `CheckBox`, `RadioButton`, `ListBox`, `ComboBox`, `ListView`,
  `NumericUpDown`, `TrackBar`, `ProgressBar`, `Panel`, `StackPanel`, `UserControl`, `GroupBox`,
  `PictureBox`, `MenuStrip`, `Timer`, `TabControl`, `SplitContainer`, `ToolStrip`, `TreeView`, `DataGridView` (lecture
  seule, tri par en-tête, `DataSource`), `DateTimePicker`, `ColorDialog`, `SegmentedControl`, `VScrollBar`, `ContextMenuStrip`, `Clipboard` (`Widgets.cs`), `CodeEditor`, `DocumentTabs` (`Editing.cs`).
* **Menus** : `Form.MainMenuStrip`, `MenuStrip` et `ToolStripMenuItem` vont à la barre de menus du bureau, pas dans la
  fenêtre (un seul niveau d'entrées sous chaque menu). `Enabled`, `Checked`, `CheckOnClick`, `Visible`, `Text` changent
  en marche. `ShortcutKeys` : une lettre, un chiffre ou F1 à F12, avec Control, Shift, Alt ; le raccourci agit dans chaque
  fenêtre qui a son menu, sauf si le contrôle au focus prend déjà la touche. `new ToolStripSeparator ()` ou `Add ("-")` : un trait.
* **Mise en page** : `Dock`, avec `Margin` (le vide autour d'un contrôle docké ; un contrôle docké caché ne prend pas de place), `Anchor`, `OnLayout` pour un contrôle qui place ses enfants. Une marge trop grande pour son panneau donne une taille négative : à éviter dans les `.ui`.
* **Fenêtres** : `Form`, `Show` (non modal, plusieurs fenêtres par programme), `ShowDialog`, `MessageBox`,
  `OpenFileDialog`, `SaveFileDialog`, `FolderBrowserDialog`, `Application.Run`, `Application.OpenForms`.
  Le programme finit avec la fenêtre principale, et les autres avec lui. `Form.Hide` n'existe pas : Elegant
  ne cache pas une fenêtre.
* **Dessin** : un `Control` à soi, ou un `Panel` / `UserControl` qui redéfinit `OnPaint`, est une vue d'UIKit
  (`ui_view`). `Graphics` dessine rectangles, lignes, ellipses, polygones, texte et images ; `Invalidate`
  redessine ; `ThemeColors` donne les couleurs du bureau, à prendre pour suivre le thème. `Pen`, `SolidBrush`,
  `Image`, `Font` (nom, taille, style) sont dans `drawing/Drawing.cs` et `drawing/Font.cs`.
* **Souris et clavier** : `MouseDown`, `MouseUp`, `MouseMove`, `MouseWheel`, `MouseEnter`, `MouseLeave`,
  `DoubleClick`, `KeyDown`, `KeyPress`, seulement sur ces vues : les widgets d'UIKit (`Button`...) gardent les
  leurs et ne donnent que `Click` et `TextChanged`. Pas de `KeyUp` : la vue ne le reçoit pas.
* **Taille préférée** : `AutoSize`, `PreferredSize`, `GetPreferredSize`, d'après les mesures d'UIKit, pour le
  texte, les boutons, les cases, les panneaux et `StackPanel`.

**`.ui` compilés** : `pibuild` les passe à `csc` (`tools/dotnet/uigen`, un générateur source de Roslyn).
`MainForm.ui` donne une partie de `partial class MainForm` : le contrôle racine en classe de base, un champ typé
par `Name`, `InitializeComponent`, et un constructeur si la classe n'en a pas. Aucun XML n'est lu à l'exécution.
Une erreur du `.ui` (contrôle, propriété, valeur, méthode inconnus) sort de la compilation avec sa ligne. Le
code du programme est dans `MainForm.cs`, à côté. Essai : `tools/dotnet/uidemo`.

**Fichiers `.ui` lus à l'exécution** (pour l'aperçu d'un éditeur) : un XML décrit une fenêtre ou un user control, et
`System.Windows.Forms.Markup.UiLoader` le lit par réflexion (`Load`, `Load<T>`, `Fill`) : un élément par
contrôle, attributs vers les propriétés, événements vers les méthodes du code-behind, `Name` vers ses
champs, `Include Source=` pour un user control.

Sous la racine d'une fenêtre, compilé ou lu à l'exécution :

```xml
<MenuStrip>
  <ToolStripMenuItem Text="Fichier">
    <ToolStripMenuItem Name="save" Text="Enregistrer" ShortcutKeys="Control|S" Click="OnSave"/>
    <ToolStripSeparator/>
    <ToolStripMenuItem Text="Quitter" Click="OnQuit"/>
  </ToolStripMenuItem>
</MenuStrip>
```

```xml
<Form Text="Bonjour" Width="460">
  <StackPanel Dock="Fill">
    <TextBox Name="name" />
    <Button Text="Saluer" Click="OnGreet" />
  </StackPanel>
</Form>
```

Essai : `tools/dotnet/formsdemo` : texte, boutons, boîte de message, sélecteur de fichier, journal.

## Préchargement des DLL

Une DLL cochée est lue une fois, puis partagée entre les programmes. **Réglages > .NET** liste les DLL de
`/dotnet` avec leur taille ; **Appliquer** écrit `SD0:/etc/dllpreload.txt` et lance `/dotnet/dllpreload`,
qui accorde la mémoire à la liste (les décochées sont rendues). `startup.txt` le relance à chaque
démarrage (`dllpreload boot` : rien si la liste est vide).

Le `mmap` d'un fichier de 64 Ko à 128 Mo met déjà ses pages dans un objet nommé de mémoire partagée,
vu en **copie sur écriture** (`SHM_MAP`, `VM_COW`, [POSIX.md](POSIX.md)) : les programmes qui tournent en
même temps les partagent, et l'objet part avec le dernier. Seules les DLL cochées restent sans process :
`dllpreload` les épingle (`file_pin`, `file_unpin`, quelle que soit leur taille). Un fichier modifié prend
un autre objet ; son ancienne épingle reste jusqu'au redémarrage.

Coût : la taille des DLL cochées en mémoire, plus les pages que le programme modifie (relocations). 256
objets nommés au plus dans le noyau.

| `hello` sous QEMU | Durée |
|---|---|
| sans préchargement | 23 s |
| `System.Private.CoreLib` gardée (16,7 Mo) | 12 s |

Sur le Pi, `hello` démarre en environ 3 s sans préchargement (31/10/2026, au chronomètre) : la carte y est bien plus rapide que celle de QEMU, et le gain devient difficile à voir.

## Ce qui marche (QEMU, 31/10/2026)

`dotnetcheck` : **39 réussis, 0 échec** (`tools/dotnet/check/Program.cs`).

| Domaine | Essayé |
|---|---|
| Langage | chaînes, format, nombres, collections, LINQ, génériques, réflexion, `Span`, `stackalloc`, `BigInteger`, SIMD |
| Exceptions | `try` / `finally`, relance, `NullReferenceException` par faute matérielle, division par zéro, indice hors limites |
| Threads | `Thread`, `lock`, `Monitor`, `ThreadPool`, `Task`, `async` / `await`, `Parallel.For`, `ConcurrentDictionary`, 100 000 appels emboîtés |
| Mémoire | 100 Mo d'allocations, `GC.Collect`, objets gardés, finaliseurs |
| Texte | `Regex`, `JsonDocument`, UTF-8 |
| Système | fichiers, dossiers, `FileStream` asynchrone, `DateTime`, `Stopwatch`, `Environment`, `Process.Start` |
| Réseau | sockets (`epoll` émulé sur `poll`), `HttpClient`, vers `10.0.2.2` |
| Cryptographie | `SHA256`, OpenSSL lié à l'hôte |
| HTTP | `HttpClient` en http et https (TLS 1.2 et 1.3, `/etc/ca-bundle`), annulation, gzip, deflate, redirections : `tools/dotnet/courier/test` (1er/11/2026) |

## Le Studio

`/apps/studio.app/main [dossier]` (`tools/dotnet/studio`) : une ébauche d'IDE en une fenêtre, en C# sur ce
WinForms. Arbre du projet, un onglet par fichier ; un `.ui` montre son rendu en haut (`UiLoader.Preview`) et son
texte en bas, un `.cs` son texte en plein ; les propriétés du contrôle sous le curseur ; Compiler lance `pibuild`,
un double-clic sur une erreur mène à sa ligne. Le texte est en couleurs : C# dans un `.cs`, XML dans un `.ui` ou un `.xml` (`CodeEditor.Language`, `UTA_CS` et `UTA_XML` d'UIKit, `src/kits/uikit/uikit_lex.inc`).

La sortie de la compilation se replie (Affichage > Sortie, F4, ou le bouton ≡). Elle est repliée au départ en 1024×768 et moins, et Compiler l'ouvre.

**Bibliothèques** : Fichier > Nouvelle bibliothèque crée un projet dans `SD0:/projets`, avec `studio.txt` (`type = lib`) ; Compiler lance `pibuild lib`, qui installe `/dotnet/nom.dll` pour tous les programmes. Projet > Construire une bibliothèque change le type d'un projet. Dans une application, Projet > Ajouter une bibliothèque interne crée `lib/nom/nom.cs` : une DLL à côté de l'application.

Raccourcis : Ctrl+N, Ctrl+O, Ctrl+S, F4 sortie, F5 exécuter, F6 compiler.

## Courier en C#

`tools/dotnet/courier` : le client HTTP d'Onyx réécrit en C# sur ce WinForms, ses interfaces en `.ui` compilés. Il a servi de banc d'essai (il a révélé des bugs d'`epoll`, de `recv`, de `ui_rbox`, d'Elegant) et **ne va plus sur la carte** : il mettait trop de temps à démarrer, l'application est maintenant le port en C++, `src/apps/courier` (son `README.md`). `kits.sh` ne le construit plus ; les sources restent, avec `tools/dotnet/courier/test` : `unit.sh` (le moteur sur le PC, sans fenêtre), `server.py` (http, https, gzip, attente infinie..., sert aussi d'essai au port en C++), `nettest`, `bigtest`, `pathtest`, `seed/` (une collection d'essai, à copier dans `/home/courier`).

**Sous QEMU, lancer une application .NET depuis le bureau**, pas par telnet ni `startup.txt` : elle n'y ouvre pas sa fenêtre. Le clavier de l'OS est en AZERTY (`qemu_drive.py type` raisonne en QWERTY) et le préchargement de Jet occupe la carte 100 à 140 s : le vider dans une image d'essai.

## Démarrage et JIT

`formsdemo --once` sous QEMU, `System.Private.CoreLib` gardée :

| Nos DLL | Durée | JIT |
|---|---|---|
| IL | 22,5 à 23 s | 8,1 à 9,1 s, 196 méthodes |
| précompilées par crossgen2 (dans WSL) | 22,4 à 23,2 s | 1,1 s, 67 méthodes |

Le temps du JIT baisse mais pas le total, pour une raison pas encore établie : crossgen2 n'entre donc pas
dans la construction, et il ne sera pas porté dans l'OS. L'hôte prend 9 s de ce temps à se charger.

## Refaire l'installation dans WSL

`sudo sh tools/dotnet/setup.sh apt`, puis `sh tools/dotnet/setup.sh` : le SDK du PC, celui d'arm64, le
runtime et crossgen2 dans `~/armpi-dotnet`, aux versions exactes, sommes vérifiées. Le script dit ce qu'il
reste à faire et ce qui manque (chaîne d'Onyx, ICU).

## Ce qui manque encore

* **Globalisation** : mode invariant (`InvariantGlobalization`), pas d'ICU à l'exécution.
* **`bind` sur le port 0** : aucun port n'est choisi, `TcpListener` doit en donner un.
* **`getgroups`** : `gid_t` de newlib fait 16 bits, `System.Native` en attend 32 ; sans effet avec uid 0.
* **`SIGCHLD`** : le noyau l'envoie, mais `Process` relève encore ses fils par `__posix_exited_child`.
* **Pas sur le Pi** : les essais s'arrêtent à QEMU pour l'instant (décision du 30/10/2026).
* **ChaCha20** : une poignée TLS 1.2 limitée à ce chiffrement échoue, cause non établie ; AES-GCM passe en TLS 1.2 et 1.3.
* **Non essayés** : débordement de pile managée, ramasse-miettes sous charge multi-cœurs longue,
  arrêt des threads pendant un calcul long.

## Ce qu'il a fallu adapter

| Point | Choix |
|---|---|
| `__linux__` | défini à la main : ouvre les chemins Linux de la PAL, de libunwind, de minipal |
| `-lpthread`, `-ldl`, `-lrt`, `-lgcc_s` | bibliothèques vides : tout est dans libposix et libgcc |
| `-pie`, `relro`, `--build-id` | retirés : les programmes d'ARMPI sont `ET_EXEC` |
| `_U` | `<ctype.h>` de newlib le définit, ce qui casse `_Uaarch64_*` de libunwind : `#undef` |
| Cookie GS | `const` dans le code : les pages de code sont partagées et jamais inscriptibles ; mis dans les données, sans `VirtualProtect` |
| `CallDescrWorkerInternalReturnAddressOffset` | aligné sur 8 : lu sans GOT en code non PIC |
| `runtimeinfo`, `createdump`, `dbgutil` côté debug, Kerberos, LTTng, canal du débogueur | hors de l'hôte |
| `/proc/self/exe` | `readlink` et `realpath` de libposix, d'après `argv[0]` et `PATH` |
