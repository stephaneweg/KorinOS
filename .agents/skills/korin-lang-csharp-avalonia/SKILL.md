---
name: korin-lang-csharp-avalonia
description: Écrire une application Avalonia 12.1.4 pour Korin OS — même .dll que sous Windows, hôte /devtools/dotnet/dotnet-skia, XAML compilé dans l'OS par csbuild, thème du bureau par Fluent, rendu GPU par Mesa sur le Pi, Studio Avalonia (avstudio) et son éditeur d'interface. À charger pour toute app C# en .axaml ; charger d'abord korin-lang-csharp pour le runtime et pibuild.
---

# Avalonia sous Korin

Une application Avalonia **ordinaire** (XAML compilé, Fluent) tourne sous Windows et sous Korin avec le **même `.dll`** :
notre `Avalonia.Desktop.dll` (même nom, version, clé publique) remplace `UsePlatformDetect` et dessine dans une surface
d'UIKit. L'application ne sait rien de Korin.

## Où lire

| Sujet | Dans le dépôt |
|---|---|
| Portage, plateforme, ce qui est prouvé, limites, mesures | `docs/AVALONIA.md` |
| `.csproj` dans l'OS, csbuild | `docs/DOTNET.md`, « Projets .csproj » |
| GPU (Mesa, Skia Ganesh) | `docs/MESA.md` |
| La plateforme | `tools/avalonia/desktop/` (`ArmpiGl.cs`, `ArmpiTheme`, `ArmpiSettings`) |
| Exemples | `tools/avalonia/avdemo` (liste, saisie, seconde fenêtre, dialogue), `skel/sd0/home/projects/gpudemo` (animation, GPU ; sur la carte `HOME:/projects/gpudemo`), `tools/avalonia/xamlload` (XAML lu à l'exécution) |
| Le Studio Avalonia | `tools/avalonia/studio` → `/apps/avstudio.app` |

## Outils

- Sur la carte : `/devtools/dotnet/dotnet-skia` (hôte avec Skia m119 et HarfBuzz 8.3.1), les DLL d'Avalonia 12.1.4 dans `/devtools/dotnet`,
  les tâches XAML dans `/devtools/dotnet/avalonia`, les références dans `/devtools/dotnet/ref/packages`. Paquet qui les apporte : posés par
  `tools/avalonia/stage.sh` dans `build/avalonia/sdroot`, absents de `tools/pkg/packages.ini` sous ce nom (à vérifier).
- **Compiler dans l'OS** : `pibuild <dossier> <nom>` sur un dossier avec un `.csproj` qui référence `Avalonia` →
  csbuild lance Roslyn, `Avalonia.Generators`, `GenerateAvaloniaResourcesTask`, `CompileAvaloniaXamlTask` ; installe
  `/apps/<nom>.app/main` et pose `main.skia` à côté. Ou bouton Compiler (F6) / Exécuter (F5) du Studio.
- **Lancer** : `/apps/<nom>.app/main [args]`, ou depuis le lanceur. AppKit voit `main.skia` et prend `dotnet-skia`.
- **Sur le PC** : `dotnet build <projet> -c Release` (avec `AVALONIA_TELEMETRY_OPTOUT=1`) ; le `.dll` produit se copie tel
  quel en `/apps/<nom>.app/main`, avec un fichier `main.skia` (contenu indifférent : `echo skia > main.skia`) et `app.txt`.

## Structure d'un programme

```
<projet>/
  <projet>.csproj          OutputType WinExe, net10.0, PackageReference Avalonia, Avalonia.Desktop, Avalonia.Themes.Fluent 12.1.4
  app.txt                  name, category (comme une app C)
  Program.cs               Main -> BuildAvaloniaApp ().StartWithClassicDesktopLifetime (args)
  App.axaml, App.axaml.cs  <FluentTheme />, MainWindow posée dans OnFrameworkInitializationCompleted
  MainWindow.axaml(.cs)    x:Class, InitializeComponent (), champs par x:Name
```

Propriétés du `.csproj` des exemples : `UseAppHost false`, `InvariantGlobalization true`,
`AvaloniaUseCompiledBindingsByDefault true`, `Nullable enable`, `Deterministic true`.

## Utiliser les kits

Rien de spécial à faire pour l'interface : fenêtres, clavier (AZERTY), curseurs, presse-papiers texte, dialogues de
fichiers (ceux d'UIKit par `IStorageProvider`) passent par la plateforme. **Thème** : `ArmpiTheme` met les jetons `TJ_*`
dans `FluentTheme.Palettes` et `ArmpiSettings` donne clair/sombre et l'accent → utiliser les ressources dynamiques de
Fluent (`{DynamicResource SystemControlBackgroundChromeMediumBrush}`…), pas de couleur en dur pour le fond.
Les kits restent accessibles par `System.UIKit`, `System.AudioKit`… comme dans tout programme C# (`korin-lang-csharp`),
mais une telle référence rend le `.dll` non portable vers Windows.

## Exemple

Adapté de `skel/sd0/home/projects/gpudemo` (Program, App) et `tools/avalonia/avdemo/AboutWindow.axaml` (fenêtre) :

```csharp
// Program.cs
using System;
using Avalonia;
namespace Hello;
public static class Program
{
	[STAThread]
	public static int Main (string[] args) => BuildAvaloniaApp ().StartWithClassicDesktopLifetime (args);
	public static AppBuilder BuildAvaloniaApp () => AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ();
}
// App.axaml.cs
using Avalonia; using Avalonia.Controls.ApplicationLifetimes; using Avalonia.Markup.Xaml;
namespace Hello;
public partial class App : Application
{
	public override void Initialize () => AvaloniaXamlLoader.Load (this);
	public override void OnFrameworkInitializationCompleted ()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
			desktop.MainWindow = new MainWindow ();
		base.OnFrameworkInitializationCompleted ();
	}
}
```

```xml
<!-- App.axaml -->
<Application xmlns="https://github.com/avaloniaui" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             x:Class="Hello.App" RequestedThemeVariant="Default">
  <Application.Styles><FluentTheme /></Application.Styles>
</Application>
<!-- MainWindow.axaml -->
<Window xmlns="https://github.com/avaloniaui" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        x:Class="Hello.MainWindow" Title="Bonjour" Width="360" Height="170">
  <StackPanel Margin="20" Spacing="14">
    <TextBlock Text="Bonjour" FontSize="20" FontWeight="SemiBold" />
    <TextBlock Text="Une application Avalonia, compilée une seule fois." TextWrapping="Wrap" />
    <Button Name="OkButton" Content="OK" Classes="accent" HorizontalAlignment="Right" />
  </StackPanel>
</Window>
```

`MainWindow.axaml.cs` : `public partial class MainWindow : Window { public MainWindow () { InitializeComponent ();
OkButton.Click += (_, _) => Close (); } }`. Puis `pibuild /home/hello hello` et `/apps/hello.app/main`.

## Le Studio Avalonia

`/apps/avstudio.app/main` (même `.dll` sous Windows) : ouvre un `.csproj`, arbre du projet (suit `Link` et `Folder`),
éditeur AvaloniaEdit coloré, F6 compile (`/bin/pibuild <dossier> <nom>`), F5 compile puis lance
`/apps/<nom>.app/main`. **Éditeur d'interface** : le texte `.axaml` reste la référence ; chaque action visuelle y fait
une modification minimale. Aperçu réel par `AvaloniaRuntimeXamlLoader`, sélection au clic, propriétés et évènements par
réflexion (`+` crée la méthode dans le `.axaml.cs`), palette de tous les contrôles, user controls (Nouveau contrôle).
Écrits mais **pas essayés** : déposer à la souris, Monter/Descendre/Retirer, onglet Structure. Pas de poignées de
déplacement ni de complétion ; `Style`, `Template`, ressources en texte.

## Pièges

- **Lancement direct** `/devtools/dotnet/dotnet-skia app.dll` depuis le terminal : échoue en `8007ff0b` (cause non établie). Passer
  par `main` + `main.skia`.
- Sous QEMU : lancer **depuis le bureau** (pas telnet ni `startup.txt`) ; vider `preload.txt` ; 29 à 70 s jusqu'à la
  fenêtre, ~15 images/s. Sur le Pi : fenêtre en ~5 s, animation fluide.
- **GPU** : sur le Pi, Skia Ganesh rend par Mesa ; une ligne au démarrage dit `avalonia : rendu sur le GPU` ou
  `le processeur (armgl 0)`. `AVALONIA_ARMPI_GPU=0` force le processeur.
- Popups dans la fenêtre (`CreatePopup` rend null) ; les 32 px du haut sont au cadre, sauf avec
  `ExtendClientAreaToDecorationsHint="True"` (laisser libres les 120 px de droite).
- Absents : déplacer une fenêtre ouverte, `BeginResizeDrag`, icônes, glisser-déposer, image au presse-papiers, suivi du
  thème à chaud (lu au démarrage). Rendu sur un seul thread ; ce qu'un autre thread poste attend jusqu'à 10 ms.
- Premier aperçu du Studio lent (plus d'une minute sous QEMU, fenêtre figée) ; liaisons et types C# du projet non
  résolus dans l'aperçu.
- Pas de NuGet dans l'OS : un paquet absent de `/devtools/dotnet/ref/packages` donne une erreur.
- Police par défaut : DejaVu Sans (`/fonts`).
