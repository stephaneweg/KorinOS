---
name: korin-lang-csharp-winforms
description: Écrire une application WinForms pour Korin OS — System.Windows.Forms réécrit sur les widgets d'UIKit, avec l'identité d'assembly de Windows (même .dll des deux côtés dans l'API commune), fenêtres décrites en .ui compilés, menus dans la barre du bureau, Studio WinForms. À charger pour toute app C# en System.Windows.Forms ; charger d'abord korin-lang-csharp pour le runtime et pibuild.
---

# WinForms sous Korin

`System.Windows.Forms` (`tools/dotnet/forms/`) a l'API de WinForms, **écrite pour Korin** (pas de code de Mono ni de
Microsoft) : chaque contrôle est un widget d'UIKit, donc le thème du bureau s'applique. Nos trois assemblys
(`System.Windows.Forms`, `System.Windows.Forms.Primitives`, `System.Drawing.Common`) portent le nom, la version 10.0.0.0
et la clé publique de Windows : un binaire compilé contre le vrai WinForms tourne ici s'il reste dans l'API commune.

## Où lire

| Sujet | Dans le dépôt |
|---|---|
| Contrôles, menus, mise en page, dessin, `.ui`, Studio | `docs/DOTNET.md`, « WinForms sur UIKit », « Le Studio » |
| Portabilité Windows ↔ Korin, écarts d'API et de comportement | `docs/WINFORMS-PORTABLE.md` |
| Sources | `tools/dotnet/forms/` (`Forms.cs`, `Layout.cs`, `Widgets.cs`, `Editing.cs`, `Markup.cs`, `drawing/`, `primitives/`) ; générateur `.ui` : `tools/dotnet/uigen` |
| Exemples | `tools/dotnet/uidemo` (`.ui` compilé, menus), `formsdemo` (dialogues, fichiers), `portable/wfhello` (projet SDK Windows ordinaire, Designer) ; Studio : `tools/dotnet/studio` |
| Vérifier un binaire | `tools/dotnet/portable/apidiff` (`check`, `surface`, `list`, sur le PC) |

## Outils

- Les DLL sont dans `/dotnet` (paquet `dotnet`) et leurs références dans `/dotnet/ref`.
- **Dans l'OS** : `pibuild app <dossier> <nom>` sur un dossier de `.cs` et `.ui` (les `.ui` sont compilés par `uigen`,
  pas installés) → `/apps/<nom>.app/main`. Avec un `.csproj` : `pibuild <dossier> <nom>` (csbuild ; `UseWindowsForms`
  ajoute `System.Drawing` et `System.Windows.Forms` aux usings implicites). Essai : `pibuild /dotnet/test/uidemo uidemo`.
- **Studio WinForms** : `/apps/studio.app/main [dossier]` — arbre, onglets, aperçu d'un `.ui` (`UiLoader.Preview`),
  propriétés, F6 compile (`pibuild`), F5 exécute, F4 sortie ; Fichier > Nouvelle bibliothèque (`studio.txt`,
  `type = lib`). Projets dans `SD0:/projets`.
- **Depuis Windows** (voie A) : projet SDK `net10.0-windows`, `UseWindowsForms true`, compilé par `dotnet build` ; copier
  le `.dll` **tel quel** en `/apps/<nom>.app/main` avec un `app.txt` (pas l'apphost `.exe`).
- Lancer : `/apps/<nom>.app/main` (AppKit voit `MZ` et lance `/dotnet/dotnet`).

## Structure d'un programme

```
<app>/
  MainForm.ui     la fenêtre en XML : partial class MainForm, champs par Name, InitializeComponent
  MainForm.cs     partial class MainForm : le code, les gestionnaires nommés dans le .ui, Main
  app.txt         name, category
```

`.ui` compilé : le contrôle racine devient la classe de base, chaque `Name` un champ typé, un constructeur est écrit si
la classe n'en a pas ; une erreur du `.ui` sort à la compilation avec sa ligne. Lu à l'exécution (aperçu) :
`System.Windows.Forms.Markup.UiLoader` (`Load`, `Load<T>`, `Fill`).

Contrôles : `Button`, `Label`, `TextBox`, `CheckBox`, `RadioButton`, `ListBox`, `ComboBox`, `ListView`, `NumericUpDown`,
`TrackBar`, `ProgressBar`, `Panel`, `StackPanel`, `UserControl`, `GroupBox`, `PictureBox`, `MenuStrip`, `Timer`,
`TabControl`, `SplitContainer`, `ToolStrip`, `TreeView`, `DataGridView` (lecture seule), `DateTimePicker`, `ColorDialog`,
`SegmentedControl`, `VScrollBar`, `ContextMenuStrip`, `Clipboard`, `CodeEditor`, `DocumentTabs`. Fenêtres : `Form`,
`Show`, `ShowDialog`, `MessageBox`, `OpenFileDialog`, `SaveFileDialog`, `FolderBrowserDialog`, `Application.Run`.

## Utiliser les kits

L'interface passe seule par UIKit : `System.Windows.Forms` → `System.UIKit` (`UI`) → `System.Kits` → P/Invoke `libarmpi`
→ `armpi_call`. **Thème** : `ThemeColors` donne les couleurs du bureau (à prendre dans `OnPaint`) ; `BackColor` et
`ForeColor` sont ignorées exprès. Dessin : un `Control`, `Panel` ou `UserControl` qui redéfinit `OnPaint` est une
`ui_view` ; `Graphics` (rectangles, lignes, ellipses, polygones, texte, images), `Pen`, `SolidBrush`, `Font`, `Invalidate`.
Les autres kits : par `System.AudioKit`, `System.NetKit`… (`korin-lang-csharp`) ; `StackPanel`, `SegmentedControl`,
`CodeEditor`, `DocumentTabs`, `ThemeColors`, `UiLoader` sont **à nous** : leur usage rend l'app non portable vers Windows.

## Exemple

`tools/dotnet/uidemo` (compilé par `pibuild` dans l'OS, code 0), `.ui` réduit :

```xml
<Form Text="Interface compilee" Width="420" Height="240">
  <MenuStrip>
    <ToolStripMenuItem Text="Journal">
      <ToolStripMenuItem Text="Saluer" ShortcutKeys="Control|G" Click="OnSaluer"/>
      <ToolStripSeparator/>
      <ToolStripMenuItem Name="clear" Text="Effacer" ShortcutKeys="F3" Click="OnEffacer"/>
    </ToolStripMenuItem>
  </MenuStrip>
  <StackPanel Dock="Fill" Padding="12" Spacing="8">
    <Label Text="Votre nom :"/>
    <TextBox Name="nom" Text="ARMPI"/>
    <Button Text="Saluer" Width="90" Click="OnSaluer"/>
    <ListBox Name="journal" Height="70"/>
  </StackPanel>
</Form>
```

```csharp
using System;
using System.Windows.Forms;

partial class MainForm
{
	public MainForm () { InitializeComponent (); }
	void OnSaluer (object sender, EventArgs e) => journal.Items.Add ("Bonjour, " + nom.Text + " !");
	void OnEffacer (object sender, EventArgs e) => journal.Items.Clear ();
	static void Main (string[] args) => Application.Run (new MainForm ());
}
```

## Pièges

- **Menus** : `MenuStrip` va à la barre de menus du bureau, pas dans la fenêtre ; un seul niveau sous chaque menu.
  `ShortcutKeys` : lettre, chiffre ou F1–F12 avec Control/Shift/Alt.
- Les widgets d'UIKit (`Button`…) ne donnent que `Click` et `TextChanged` ; souris et clavier (`MouseDown`, `KeyDown`,
  `KeyPress`…) seulement sur les vues dessinées. **Pas de `KeyUp`**.
- `Form.Hide` n'existe pas (Elegant ne cache pas une fenêtre). Le programme finit avec la fenêtre principale.
- **`Margin`** : ici le vide autour d'un contrôle docké ; à Windows `Dock` l'ignore. Une marge trop grande donne une
  taille négative. `Form.Size` = taille du contenu (à Windows, cadre compris). `AutoScaleDimensions` ignoré.
- `DrawRectangle` ignore l'épaisseur du `Pen` (et n'a montré que deux côtés dans une capture, cause non établie).
- `Segoe UI` tombe sur DejaVu Sans : largeurs de texte différentes.
- Portabilité : seulement 605 membres identiques sur 18 163 ; absents entre autres `Control.Handle` en `IntPtr`,
  `WndProc`, `CreateParams`, `TableLayoutPanel`, `FlowLayoutPanel`, `Brushes`, `Pens`, `Icon`, `Bitmap (Stream)`,
  `RichTextBox`, ressources `.resx` du Designer. Une référence sans cible lève une exception **quand le JIT compile la
  méthode**, pas au chargement : passer le binaire à `apidiff check`. P/Invoke vers `user32`/`gdi32` : impossible.
- Tout membre public ajouté à nos DLL doit exister à Windows sur le même type, sinon le binaire casse sous Windows.
- Démarrage : 40 à 50 s jusqu'à la fenêtre sous QEMU ; lancer depuis le bureau, `preload.txt` vidé. Le chemin Windows →
  Korin n'a été prouvé que sur `wfhello`, sous QEMU ; le Pi n'a pas été essayé pour WinForms (à vérifier).
