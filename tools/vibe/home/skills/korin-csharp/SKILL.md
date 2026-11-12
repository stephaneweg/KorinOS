---
name: korin-csharp
description: Écrire, compiler dans l'OS (pibuild, Roslyn de /dotnet, projets .csproj) et installer un programme ou une app en C# sous Korin OS - .NET 10, WinForms sur UIKit (fenêtres .ui compilées), Avalonia, les kits en C#. À utiliser pour tout programme C# ou .NET sur Korin.
user-invocable: true
---

# C# et .NET sous Korin OS

**CoreCLR de .NET 10.0.14**, avec son JIT, sa bibliothèque et le compilateur C# (Roslyn), tourne dans Korin : on
écrit, compile et lance du C# **sur la machine**. Le runtime partagé est dans **`/dotnet`** (s'il manque :
`pkg add dotnet`). Pas de commande `dotnet build` ni de NuGet :
c'est **`pibuild`** qui compile.

## Compiler et installer

* Un dossier avec des `.cs` (sans `.csproj`) :

  ```sh
  pibuild app /sd0/home/projects/<nom> <nom>     # -> /apps/<nom>.app/main (une DLL), avec app.txt et data/
  pibuild bin /sd0/home/projects/<nom> <nom>     # -> /bin/<nom> (programme de terminal)
  pibuild lib /sd0/home/projects/<nom> <nom>     # -> /dotnet/<nom>.dll, une bibliothèque pour tous les programmes
  ```

  Tous les `.cs` du dossier et des sous-dossiers sont compilés (pour une app, chaque `lib/<x>/` devient `x.dll` à
  côté). Les `.ui` (fenêtres WinForms en XML) sont compilés avec le code. Réussi quand la dernière ligne est
  `Installe : /apps/<nom>.app/main`.
* Un dossier avec un **`.csproj`** : `pibuild /sd0/home/projects/<nom> <nom>`, et c'est le projet qui décide
  (`OutputType` `Exe`/`WinExe` → app ; `Library` → `/dotnet`). Lu par `csbuild`, un sous-ensemble de MSBuild :
  `Compile`, `Reference` (`/dotnet/ref`), `ProjectReference`, `ImplicitUsings`, `Nullable`, `LangVersion`,
  `DefineConstants`, `Condition` simples. `PackageReference` seulement pour ce qui est dans `/dotnet/ref` ou
  `/dotnet/ref/packages` (Avalonia, Newtonsoft.Json) : **pas de NuGet**. `Import`, `Target` : ignorés.
* **Lent** : la première compilation prend une à deux minutes sous QEMU (Roslyn démarre) ; un projet à jour n'est
  pas recompilé. Ne pas relancer parce que ça tarde.
* Le `main` installé est une DLL : AppKit la lance par `/dotnet/dotnet`. Une app graphique ne se lance **jamais au
  premier plan** dans le shell : `(/apps/<nom>.app/main >/dev/null 2>&1 &)`, ou par le lanceur. Un programme de
  terminal (`bin`) se lance normalement.

## Le modèle : WinForms (sur UIKit)

`System.Windows.Forms` de Korin a l'API de WinForms, mais chaque contrôle est un widget d'UIKit : le thème du bureau
(Givre, Onyx Noir) s'applique tout seul. Le dossier : `MainForm.cs`, `MainForm.ui`, `app.txt` (même manifeste qu'en
C, compétence `korin-app`).

`MainForm.ui` (compilé : `partial class MainForm`, un champ par `Name`, `InitializeComponent`) :

```xml
<Form Text="Compteur" Width="420" Height="240">
  <MenuStrip>
    <ToolStripMenuItem Text="Fichier">
      <ToolStripMenuItem Text="Remettre à zéro" ShortcutKeys="Control|R" Click="OnReset"/>
    </ToolStripMenuItem>
  </MenuStrip>
  <StackPanel Dock="Fill" Padding="24" Spacing="12">
    <Label Name="count" Text="Clics : 0"/>
    <Button Text="Cliquer" Click="OnClick"/>
  </StackPanel>
</Form>
```

`MainForm.cs` :

```csharp
// MainForm.cs -- <ce que fait l'app> (Korin OS, WinForms sur UIKit)
using System;
using System.Windows.Forms;

partial class MainForm
{
	int clicks;

	void OnClick (object sender, EventArgs e) => count.Text = "Clics : " + ++clicks;
	void OnReset (object sender, EventArgs e) => count.Text = "Clics : " + (clicks = 0);

	[STAThread]
	static void Main (string[] args) => Application.Run (new MainForm ());
}
```

Ce que WinForms sait ici : `Button`, `Label`, `TextBox`, `CheckBox`, `RadioButton`, `ListBox`, `ComboBox`,
`ListView`, `NumericUpDown`, `TrackBar`, `ProgressBar`, `Panel`, `StackPanel` (à Korin : `Horizontal`, `Spacing`,
`Padding` ; vertical, il étire ses enfants sur toute la largeur), `GroupBox`, `PictureBox`, `TabControl`, `SplitContainer`, `ToolStrip`, `TreeView`, `DataGridView`
(lecture seule), `DateTimePicker`, `Timer`, `MessageBox`, `OpenFileDialog`, `SaveFileDialog`,
`FolderBrowserDialog`, `ShowDialog`, plusieurs `Form`. Mise en page : `Dock`, `Anchor`, `Margin`. **Les menus vont
dans la barre de menus du bureau** (un niveau sous chaque menu). Dessin à soi : un `Control` ou `Panel` qui redéfinit
`OnPaint` (`Graphics`), avec les couleurs de **`ThemeColors`** (`Face`, `Surface`, `Line`, `Text`, `TextDim`,
`Accent`, `Danger`) pour suivre le thème. Pas de `Form.Hide`, pas de `KeyUp`.

Les règles d'une app de Korin valent ici (compétence `korin-app`) : pas de couleurs en dur, coin haut droit libre
(pas de barre de titre : `Dock` le respecte ; un contrôle placé à la main en haut à droite non), mise en page par
`Dock`/`Anchor` pour le **mode poche** (fenêtre en plein écran 800 × 600), textes en français, noms en anglais.

## Autres voies

* **Avalonia 12.1.4** : un `.csproj` avec `PackageReference` `Avalonia`, `Avalonia.Desktop`,
  `Avalonia.Themes.Fluent` (version `12.1.4`), `OutputType` `WinExe`, XAML (`.axaml`) compilé par `pibuild`. Le même
  `.dll` tourne sous Windows. Démarrage lent sous QEMU.
* **Les kits directement** : `System.UIKit` (classe `UI` : les `ui_*` et les `TJ_*`), `System.AudioKit` (`AK`),
  `System.NetKit` (`NK`), `System.GpuKit` (`GK`), `System.MediaKit` (`MK`), `System.DocumentKit` (`DK`, `Document`,
  `Spreadsheet`, `Presentation`), `System.3D` (`Viewport3D`) : le kit s'ouvre au premier appel. Mêmes fonctions qu'en C
  (guide : compétence `korin`, `reference/`).
* La bibliothèque standard : LINQ, `Task`/`async`, `HttpClient` (https compris), `JsonDocument`, `Regex`, fichiers,
  `Process.Start`, sockets IPv4, Newtonsoft.Json.

## Ce qui manque

* Globalisation invariante (pas d'ICU) : cultures, formats de dates et de nombres neutres.
* `TcpListener` sur le port 0 ; IPv6 ; NuGet ; MSBuild complet (cibles, tâches) ; `dotnet` en ligne de commande.
* Une API WinForms absente ici ne compile pas : chercher la classe dans `/dotnet/ref` avant de l'utiliser, ou
  rester dans la liste plus haut.
