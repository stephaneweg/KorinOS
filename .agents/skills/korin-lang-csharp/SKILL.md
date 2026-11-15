---
name: korin-lang-csharp
description: Écrire, compiler dans l'OS et lancer un programme C# sous Korin OS — CoreCLR .NET 10 dans /devtools/dotnet, Roslyn et csbuild par pibuild, projets .csproj, bibliothèques, appels aux kits par System.UIKit, System.AudioKit et les autres. À charger pour tout programme C# ; pour une interface, charger aussi korin-lang-csharp-avalonia ou korin-lang-csharp-winforms.
---

# C# sous Korin

CoreCLR **.NET 10.0.14** (JIT, bibliothèque de `linux-arm64` inchangée, Roslyn) tourne dans l'OS : Korin se fait passer
pour Linux arm64. Code porté, précompilé sur le PC, hors du selfhost.

## Où lire

| Sujet | Dans le dépôt |
|---|---|
| Runtime, `/devtools/dotnet`, pibuild, `.csproj`, préchargement, ce qui marche et manque | `docs/DOTNET.md` |
| Règles de pibuild pour `.cs` et `.csproj` | `docs/GCC.md` (« pibuild ») ; `tools/gcc/pibuild.mk` (sur la carte : `/devtools/gcc/share/pibuild.mk`) |
| Liaisons des kits | `tools/dotnet/kits/` (`KitLoader.cs`, `Ui.cs`), générées par `tools/kits/abi2cs.py` dans `tools/dotnet/kits.sh` |
| Fonctions d'un kit (noms C, identiques en C#) | compétences `korin-kit-<kit>`, `/apps/help.app/api/<kit>_fr.md` |
| Essais | `tools/dotnet/cshello` (console + AudioKit), `libdemo`, `libuse`, `jsondemo`, `docdemo`, `check` ; sur la carte `/devtools/dotnet/test/` |

## Outils

Paquet `dotnet` (`pkg add dotnet`) → `/devtools/dotnet` (≈ 118 Mo) :

| Dans `/devtools/dotnet` | Quoi |
|---|---|
| `dotnet` | l'hôte statique (CoreCLR, JIT, GC, `System.Native` en un ELF) : `dotnet /chemin/prog.dll` |
| `dotnet-skia` | second hôte, avec Skia/HarfBuzz, pour Avalonia |
| `*.dll` | le framework et nos assemblys (`System.Kits`, `System.UIKit`…, WinForms, Avalonia) |
| `roslyn/csc.dll`, `csc.rsp`, `ref/` | le compilateur, ses options, les assemblys de référence ; `ref/user/` : vos bibliothèques |
| `csbuild.dll` | constructeur des `.csproj` (lancé par pibuild) |
| `Newtonsoft.Json.dll` | 13.0.4, visible de tous |

```sh
pibuild bin <dossier> <nom>       # dossier de .cs -> /bin/<nom> (une DLL)
pibuild app <dossier> <nom>       # -> /apps/<nom>.app/main, avec app.txt et data/
pibuild lib <dossier> <nom>       # -> /devtools/dotnet/<nom>.dll + /devtools/dotnet/ref/user/<nom>.dll, pour tous les programmes
pibuild <dossier> <nom>           # dossier avec .csproj : c'est le projet qui décide (csbuild)
/bin/<nom> arguments              # lancer
```

Sous QEMU, `cshello` met 141 s à compiler. Un projet à jour n'est pas recompilé (`/tmp/csbuild`). Les deux réglages
d'environnement cités dans DOTNET.md (`DOTNET_EnableWriteXorExecute=0`, `DOTNET_GCHeapHardLimit`) sont posés par l'hôte
lui-même (`setenv(..., 0)` dans `tools/dotnet/armpi.patch`) : inutile de les exporter.

**Lancement** : il n'y a pas de lanceur ; le fichier installé *est* la DLL. AppKit voit `MZ` en tête et lance
`/devtools/dotnet/dotnet` sous le nom du programme, DLL en `argv[1]` (`/devtools/dotnet/dotnet-skia` si `main.skia` est à côté).
L'hôte **ne lit ni `runtimeconfig.json` ni `deps.json`** : assemblys du dossier du programme, puis de `/devtools/dotnet`.

## Structure d'un programme

Sans projet : un dossier de `.cs` (tous compilés, `static void Main`), `.ui` compilés avec (WinForms). Pour une app,
chaque `lib/<nom>/` devient `<nom>.dll` à côté (`lib/<nom>/refs.txt` : ses dépendances dans `lib/`) ; pour les autres
types, `lib/` est fondu dans le programme. Exemple : `tools/dotnet/libdemo`.

Avec `.csproj` (le même fichier s'ouvre dans Visual Studio ; MSBuild ne tourne pas, `tools/dotnet/project` en lit un
sous-ensemble) :

| Élément | Effet |
|---|---|
| `OutputType` `Exe`/`WinExe` | programme `/apps/<nom>.app/main` ; `Library` ou absent : `/devtools/dotnet/<AssemblyName>.dll` |
| `ArmpiType` `bin`/`sys`/`svc` | autre lieu d'installation (ignoré par Visual Studio) |
| `ProjectReference`, `Reference` (`HintPath`, sinon `/devtools/dotnet/ref`) | comme le SDK |
| `PackageReference` | **pas de NuGet** : `/devtools/dotnet/ref/<Id>.dll` ou `/devtools/dotnet/ref/packages/<Id>/`, sinon erreur |
| `Compile` motifs, `Link`, `Folder`, `Condition`, `Directory.Build.props`, `ImplicitUsings`, `Nullable`, `LangVersion`… | lus |
| `Import`, `Target`, `UsingTask` | ignorés, avec avertissement |

Essai dans l'OS : `pibuild /devtools/dotnet/test/multi/app multiapp2`, puis `/apps/multiapp2.app/main`.

## Utiliser les kits

Pas de `kit_open` : une classe statique par kit, générée depuis le `.abi`, ouvre le kit à son premier appel.

| Assembly / espace de noms | Classe | Kit |
|---|---|---|
| `System.UIKit` | `UI` (fonctions `ui_*`, constantes), `Ui` (aides : `Init`, `Event`, `Token`, `With`) | UIKit |
| `System.AudioKit` | `AK` | AudioKit |
| `System.NetKit`, `System.GpuKit`, `System.MediaKit` | `NK`, `GK`, `MK` | NetKit, GpuKit, MediaKit |
| `System.DocumentKit` | `DK`, plus `Document`, `Spreadsheet`, `Presentation` | DocumentKit |
| `System.Kits` | `KitLoader` (`Open(nom, n)`, `Call`, `Utf8`, `Free`, `String`) | tout kit, à la main |

Règles : chaque argument est un `long` ; une fonction qui rend plusieurs registres a des paramètres `out`. Une chaîne
se passe en UTF-8 alloué : `Ui.With (chemin, p => UI.ui_image_load (p))` (libère au retour) ou
`KitLoader.Utf8`/`KitLoader.Free` ; une chaîne rendue par un kit se lit par `KitLoader.String (p)`. Un thème :
`Ui.Token (UI.TJ_FACE)`. **Pas de liaison pour GpioKit, ArchiveKit, PrinterKit, EmuKit** (absents de `kits.sh`) :
passer par `KitLoader.Open ("gpiokit", n)` et `KitLoader.Call` (non essayé, à vérifier).

```csharp
long hz = AK.ak_note_mhz (69) / 1000;                       // AudioKit : 440
long img = Ui.With ("/home/a.png", p => UI.ui_image_load (p));  // UIKit, chaîne
var doc = Document.Open ("/home/rapport.docx");             // DocumentKit en types C#
```

Le reste du système passe par .NET ordinaire : fichiers, `Process.Start`, sockets, `HttpClient` (http et https, TLS par
OpenSSL, `/etc/ca-bundle`), threads, `Task`, `Regex`, `JsonDocument` — 39 essais sur 39 (`tools/dotnet/check`, QEMU).

## Exemple

`tools/dotnet/cshello/Program.cs` (copié sur `/devtools/dotnet/test/cshello`), compilé dans l'OS :

```csharp
// cshello : un programme C# compile dans ARMPI par pibuild (docs/DOTNET.md), qui appelle un kit.
using System;
using System.AudioKit;
using System.Linq;

class Program
{
	static void Main (string[] args)
	{
		var notes = Enumerable.Range (60, 3).Select (n => AK.ak_note_mhz (n) / 1000).ToArray ();
		Console.WriteLine ("cshello compile sous ARMPI : " + args.Length + " argument(s), la4 = "
			+ AK.ak_note_mhz (69) / 1000 + " Hz, do mi : " + string.Join (" ", notes));
	}
}
```

```sh
pibuild bin /devtools/dotnet/test/cshello cshello && /bin/cshello un deux
```

## Pièges

- **Globalisation invariante** (pas d'ICU) : une culture réelle n'est pas disponible.
- **Pas de NuGet dans l'OS** : tout paquet doit déjà être dans `/devtools/dotnet/ref` ou `/devtools/dotnet/ref/packages`.
- Démarrage lent : ~3 s pour `hello` sur le Pi, 23 s sous QEMU. **Réglages > .NET** coche des DLL à précharger
  (`/etc/dllpreload.txt`, `/devtools/dotnet/dllpreload`) : `System.Private.CoreLib` gardée → 12 s sous QEMU.
- **Sous QEMU, lancer une app .NET depuis le bureau** (Terminal du bureau, lanceur), pas par telnet ni `startup.txt` :
  elle n'y ouvre pas sa fenêtre. Le clavier de l'OS est AZERTY. Le préchargement de Jet occupe la carte 100 à 140 s :
  le vider (`etc/preload.txt`) dans une image d'essai.
- `TcpListener` sur le port 0 : aucun port n'est choisi. TLS 1.2 limité à ChaCha20 : échoue (AES-GCM passe).
- Pour retirer une bibliothèque `pibuild lib` : effacer `/devtools/dotnet/<nom>.dll` et `/devtools/dotnet/ref/user/<nom>.dll`.
- Une application compilée pour un .NET plus récent que 10 ne se chargera pas.
- DOTNET.md dit encore « pas sur le Pi » pour les essais .NET ; plusieurs apps y ont tourné depuis (Avalonia) : l'état
  exact sur le Pi est à vérifier programme par programme.
