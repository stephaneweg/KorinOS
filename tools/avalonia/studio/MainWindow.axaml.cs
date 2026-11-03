using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Xml.Linq;
using Armpi.Project;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using AvaloniaEdit;
using AvaloniaEdit.Highlighting;

namespace AvStudio;

/// <summary>Un fichier ouvert : son onglet, son editeur, et pour un .axaml son apercu.</summary>
sealed class Document
{
	public required string Path;
	public required TextEditor Editor;
	public required TabItem Tab;
	public DesignSurface? Surface;
	public TextBlock? Title;
	public bool Dirty;
	public int Selected = -1;
	public string Name => System.IO.Path.GetFileName (Path);
	public bool IsXaml => Path.EndsWith (".axaml", StringComparison.OrdinalIgnoreCase);
}

sealed record BuildError (string File, int Line, int Column, string Text)
{
	public override string ToString () => File + " (" + Line + ") : " + Text;
}

public partial class MainWindow : Window
{
	static readonly Regex s_error = new (@"^(.+?)\((\d+),(\d+)\): (?:error|warning) (\w+: .+)$");
	readonly List<Document> _docs = new ();
	readonly DispatcherTimer _preview = new () { Interval = TimeSpan.FromMilliseconds (500) };
	readonly PropertiesPanel _properties = new (false), _events = new (true);
	List<ProjectControl> _controls = new ();
	CsProject? _project;
	Process? _running;
	string? _dragged;
	bool _applying, _keepStatus;

	public MainWindow () : this (null) { }

	public MainWindow (string? folder)
	{
		InitializeComponent ();
		PropertiesTab.Content = _properties;
		EventsTab.Content = _events;
		_properties.Changed += SetAttribute;
		_events.Changed += SetAttribute;
		_events.HandlerRequested += AddEventHandler;
		OpenButton.Click += async (_, _) => await PickProject ();
		SaveButton.Click += (_, _) => SaveAll ();
		WelcomeOpen.Click += async (_, _) => await PickProject ();
		NewFolderButton.Click += async (_, _) => await NewFolder ();
		PlaceButton.Click += async (_, _) => await PlaceFile ();
		OutputButton.Click += (_, _) => ShowLower (Lower.RowDefinitions[2].Height.Value == 0);
		BuildButton.Click += (_, _) => Build (false);
		RunButton.Click += (_, _) => Build (true);
		NewControlButton.Click += async (_, _) => await NewXaml ("UserControl");
		NewWindowButton.Click += async (_, _) => await NewXaml ("Window");
		UpButton.Click += (_, _) => Edit ((text, e) => XamlText.Move (text, e, -1), keep: false);
		DownButton.Click += (_, _) => Edit ((text, e) => XamlText.Move (text, e, 1), keep: false);
		DeleteButton.Click += (_, _) => RemoveSelected ();
		Files.SelectionChanged += (_, _) =>
		{
			if (Files.SelectedItem is TreeViewItem { Tag: string path } && File.Exists (path))
				Open (path);
		};
		Outline.SelectionChanged += (_, _) =>
		{
			if (Outline.SelectedItem is TreeViewItem { Tag: int id } && Current is { } d && d.Selected != id)
				Pick (d, id);
		};
		Tabs.SelectionChanged += (_, _) => DocumentChanged ();
		Errors.DoubleTapped += (_, _) => GoToError ();
		Palette.DoubleTapped += (_, _) =>
		{
			if (Palette.SelectedItem is string s)
				Insert (s, Current?.Selected ?? -1);
		};
		// (tirer depuis la palette : la souris suffit, sans le glisser-deposer du systeme)
		Palette.AddHandler (PointerPressedEvent, (_, e) => _dragged = (e.Source as Control)?.DataContext as string ?? (e.Source as TextBlock)?.Text, RoutingStrategies.Tunnel);
		AddHandler (PointerReleasedEvent, OnReleased, RoutingStrategies.Tunnel);
		KeyDown += OnKey;
		_preview.Tick += (_, _) =>
		{
			_preview.Stop ();
			RefreshPreview ();
		};
		Palette.ItemsSource = ControlCatalog.Palette ().ToList ();
		UpdateCommands ();
		ShowRecent ();
		Opened += (_, _) => WindowState = WindowState.Maximized;
		if (folder != null)
			Opened += (_, _) => LoadProject (folder);
	}

	Document? Current => _docs.FirstOrDefault (d => d.Tab == Tabs.SelectedItem);

	void OnKey (object? sender, KeyEventArgs e)
	{
		bool ctrl = e.KeyModifiers.HasFlag (KeyModifiers.Control);
		if (ctrl && e.Key == Key.S) SaveAll ();
		else if (e.Key == Key.F6) Build (false);
		else if (e.Key == Key.F5) Build (true);
		else if (e.Key == Key.F4) ShowLower (Lower.RowDefinitions[2].Height.Value == 0);
		else if (e.Key == Key.Delete && Current?.Surface is { IsFocused: true }) RemoveSelected ();
		else return;
		e.Handled = true;
	}

	void UpdateCommands ()
	{
		bool open = _project != null;
		Welcome.IsVisible = _docs.Count == 0;
		WelcomeOpen.IsVisible = !open;
		if (open)
		{
			WelcomeTitle.Text = _project!.Name;
			WelcomeHint.Text = "Choisissez un fichier dans l'arbre.";
			RecentTitle.IsVisible = false;
			Recent.Children.Clear ();
		}
		SaveButton.IsEnabled = open;
		NewControlButton.IsEnabled = NewWindowButton.IsEnabled = open && _project!.IsAvalonia;
		NewFolderButton.IsEnabled = PlaceButton.IsEnabled = open;
		BuildButton.IsEnabled = open && _running == null;
		RunButton.IsEnabled = open && _running == null && _project is { IsLibrary: false };
	}

	void ShowLower (bool show)
	{
		Lower.RowDefinitions[2].Height = new GridLength (show ? 140 : 0);
		LowerSplitter.IsVisible = show;
	}

	static string RecentFile => OperatingSystem.IsWindows ()
		? Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "avstudio-recent.txt")
		: "SD0:/etc/avstudio-recent.txt";

	static List<string> LoadRecent ()
	{
		try { return File.ReadAllLines (RecentFile).Where (File.Exists).ToList (); }
		catch (IOException) { return new (); }
		catch (UnauthorizedAccessException) { return new (); }
	}

	void ShowRecent ()
	{
		var recent = LoadRecent ();
		Recent.Children.Clear ();
		RecentTitle.IsVisible = recent.Count > 0;
		foreach (var path in recent.Take (5))
		{
			var b = new Button { Content = Path.GetFileNameWithoutExtension (path), Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Center };
			ToolTip.SetTip (b, path);
			b.Click += (_, _) => LoadProject (path);
			Recent.Children.Add (b);
		}
	}

	static void Remember (string path)
	{
		try
		{
			var list = LoadRecent ();
			list.RemoveAll (p => p == path);
			list.Insert (0, path);
			File.WriteAllLines (RecentFile, list.Take (8));
		}
		catch (IOException) { }
		catch (UnauthorizedAccessException) { }
	}

	// --- le projet ----------------------------------------------------------------------------

	async Task PickProject ()
	{
		var files = await StorageProvider.OpenFilePickerAsync (new FilePickerOpenOptions
		{
			Title = "Ouvrir un projet",
			FileTypeFilter = new[] { new FilePickerFileType ("Projets C#") { Patterns = new[] { "*.csproj" } } },
		});
		if (files.Count > 0 && files[0].TryGetLocalPath () is { } path)
			LoadProject (path);
	}

	void LoadProject (string path)
	{
		try
		{
			_project = CsProject.Load (path);
		}
		catch (Exception e) when (e is IOException or InvalidDataException or System.Xml.XmlException or UnauthorizedAccessException)
		{
			Status.Text = e.Message;
			return;
		}
		Remember (_project.Path);
		var root = new TreeViewItem { Header = _project.Name, IsExpanded = true };
		root.Items.Add (new TreeViewItem { Header = Path.GetFileName (_project.Path), Tag = _project.Path });
		var folders = new Dictionary<string, TreeViewItem> { [""] = root };
		foreach (var f in _project.Folders.Order (StringComparer.OrdinalIgnoreCase))
			Folder (folders, f);
		// (l'arbre suit Link : un fichier y parait ou le projet le range, pas ou il est sur le disque)
		var project = _project;
		foreach (var f in project.TreeFiles.OrderBy (project.Display, StringComparer.OrdinalIgnoreCase))
		{
			string display = project.Display (f);
			var parent = Folder (folders, Path.GetDirectoryName (display)?.Replace ('\\', '/') ?? "");
			var node = new TreeViewItem { Header = Path.GetFileName (display), Tag = Path.GetFullPath (Path.Combine (project.Directory, f)) };
			if (display != f)
				ToolTip.SetTip (node, f);
			parent.Items.Add (node);
		}
		foreach (var r in _project.ProjectReferences)
			root.Items.Add (new TreeViewItem { Header = "→ " + Path.GetFileNameWithoutExtension (r), Tag = r });
		Files.ItemsSource = new[] { root };
		Title = _project.Name + " - Studio";
		Summary.Text = (_project.IsLibrary ? "Bibliothèque" : "Programme") + ", " + _project.TargetFramework + ", " + _project.Compile.Count + " sources";
		Status.Text = _project.Warnings.Count > 0 ? string.Join (" ; ", _project.Warnings) : _project.Path;
		_controls = PreviewBuilder.UserControls (_project);
		Palette.ItemsSource = _controls.Select (c => c.Name).Concat (ControlCatalog.Palette ()).ToList ();
		UpdateCommands ();
	}

	static TreeViewItem Folder (Dictionary<string, TreeViewItem> folders, string path)
	{
		if (folders.TryGetValue (path, out var item))
			return item;
		string parent = path.Contains ('/') ? path.Substring (0, path.LastIndexOf ('/')) : "";
		item = new TreeViewItem { Header = path.Substring (path.LastIndexOf ('/') + 1), IsExpanded = true };
		Folder (folders, parent).Items.Add (item);
		folders[path] = item;
		return item;
	}

	/// <summary>Une question d'un mot, fenetre modale ; null si abandon.</summary>
	async Task<string?> Ask (string title, string value)
	{
		var name = new TextBox { Text = value };
		var ok = new Button { Content = "OK", Classes = { "accent" }, HorizontalAlignment = HorizontalAlignment.Right, IsDefault = true };
		var ask = new Window { Title = title, Width = 320, Height = 130, CanResize = false, WindowStartupLocation = WindowStartupLocation.CenterOwner };
		ask.Content = new StackPanel { Margin = new Thickness (16), Spacing = 10, Children = { name, ok } };
		ok.Click += (_, _) => ask.Close (name.Text);
		ask.Opened += (_, _) =>
		{
			name.Focus ();
			name.SelectAll ();
		};
		return await ask.ShowDialog<string?> (this);
	}

	/// <summary>Ajoute au .csproj, dans le dernier ItemGroup sans condition, ou dans un nouveau.</summary>
	void AddToProject (string item)
	{
		string path = _project!.Path;
		var open = _docs.FirstOrDefault (d => Path.GetFullPath (d.Path) == Path.GetFullPath (path));
		string text = open?.Editor.Text ?? File.ReadAllText (path);
		var x = XamlText.Parse (text);
		if (x == null)
		{
			Status.Text = "Le .csproj ne se lit pas";
			return;
		}
		var group = x.Root!.Elements ().LastOrDefault (e => e.Name.LocalName == "ItemGroup" && e.Attribute ("Condition") == null);
		text = group != null ? XamlText.AddChild (text, group, item) : XamlText.AddChild (text, x.Root, "<ItemGroup>\n    " + item + "\n  </ItemGroup>");
		if (open != null)
		{
			open.Editor.Document.Replace (0, open.Editor.Document.TextLength, text);
			Save (open);
		}
		else
			File.WriteAllText (path, text, new UTF8Encoding (false));
		LoadProject (path);
	}

	/// <summary>Un dossier du projet, comme Visual Studio : sur le disque, et dans le .csproj tant qu'il est vide.</summary>
	async Task NewFolder ()
	{
		if (_project == null)
			return;
		var name = (await Ask ("Nouveau dossier", "Dossier"))?.Trim ().Trim ('/', '\\');
		if (string.IsNullOrEmpty (name) || !Regex.IsMatch (name, @"^[\w .-]+([/\\][\w .-]+)*$"))
			return;
		Directory.CreateDirectory (Path.Combine (_project.Directory, name));
		AddToProject ("<Folder Include=\"" + name.Replace ('/', '\\') + "\\\" />");
		Status.Text = "Dossier " + name + " ajouté";
	}

	/// <summary>Range le fichier choisi dans un dossier virtuel : Link, le fichier ne bouge pas sur le disque.</summary>
	async Task PlaceFile ()
	{
		if (_project == null || Files.SelectedItem is not TreeViewItem { Tag: string path } || !File.Exists (path))
		{
			Status.Text = "Choisissez un fichier du projet";
			return;
		}
		var project = _project;
		string? file = project.TreeFiles.FirstOrDefault (f => Path.GetFullPath (Path.Combine (project.Directory, f)) == Path.GetFullPath (path));
		if (file == null)
			return;
		string current = Path.GetDirectoryName (project.Display (file))?.Replace ('\\', '/') ?? "";
		var answer = await Ask ("Placer dans le dossier", current.Length > 0 ? current : "Dossier");
		if (answer == null)
			return;
		string folder = answer.Trim ().Trim ('/', '\\').Replace ('\\', '/');
		string Line (string f)
		{
			string kind = f.EndsWith (".axaml", StringComparison.OrdinalIgnoreCase) ? "AvaloniaXaml" : "Compile";
			string link = (folder.Length > 0 ? folder + "/" : "") + Path.GetFileName (f);
			return "<" + kind + " Update=\"" + f.Replace ('/', '\\') + "\" Link=\"" + link.Replace ('/', '\\') + "\" />";
		}
		AddToProject (Line (file));
		// (un .axaml et son code restent ensemble)
		string partner = file.EndsWith (".axaml.cs", StringComparison.OrdinalIgnoreCase) ? file.Substring (0, file.Length - 3)
			: file.EndsWith (".axaml", StringComparison.OrdinalIgnoreCase) ? file + ".cs" : "";
		if (partner.Length > 0 && _project.TreeFiles.Contains (partner))
			AddToProject (Line (partner));
		Status.Text = Path.GetFileName (file) + " rangé dans " + (folder.Length > 0 ? folder : "la racine");
	}

	string RootNamespace => _project!.Property ("RootNamespace") ?? _project.AssemblyName;

	/// <summary>Un nouveau .axaml et son code, dans le projet : les motifs du .csproj les trouvent.</summary>
	async Task NewXaml (string kind)
	{
		if (_project == null)
			return;
		var name = new TextBox { Text = kind == "Window" ? "NewWindow" : "NewControl" };
		var ok = new Button { Content = "Créer", Classes = { "accent" }, HorizontalAlignment = HorizontalAlignment.Right, IsDefault = true };
		var ask = new Window { Title = kind == "Window" ? "Nouvelle fenêtre" : "Nouveau contrôle", Width = 320, Height = 130, CanResize = false, WindowStartupLocation = WindowStartupLocation.CenterOwner };
		ask.Content = new StackPanel { Margin = new Thickness (16), Spacing = 10, Children = { name, ok } };
		ok.Click += (_, _) => ask.Close (name.Text);
		ask.Opened += (_, _) =>
		{
			name.Focus ();
			name.SelectAll ();
		};
		var answer = await ask.ShowDialog<string?> (this);
		if (string.IsNullOrWhiteSpace (answer) || !Regex.IsMatch (answer, @"^[A-Za-z_]\w*$"))
			return;
		string ns = RootNamespace, path = Path.Combine (_project.Directory, answer + ".axaml");
		if (File.Exists (path))
		{
			Status.Text = answer + ".axaml existe déjà";
			return;
		}
		File.WriteAllText (path, "<" + kind + " xmlns=\"https://github.com/avaloniaui\"\n" + new string (' ', kind.Length + 2) + "xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"\n"
			+ new string (' ', kind.Length + 2) + "x:Class=\"" + ns + "." + answer + "\">\n  <StackPanel />\n</" + kind + ">\n", new UTF8Encoding (false));
		File.WriteAllText (path + ".cs", "using Avalonia.Controls;\n\nnamespace " + ns + ";\n\npublic partial class " + answer + " : " + kind + "\n{\n\tpublic " + answer + " ()\n\t{\n\t\tInitializeComponent ();\n\t}\n}\n", new UTF8Encoding (false));
		LoadProject (_project.Path);
		Open (path);
	}

	// --- les documents ------------------------------------------------------------------------

	void Open (string path)
	{
		string full = Path.GetFullPath (path);
		var doc = _docs.FirstOrDefault (d => Path.GetFullPath (d.Path) == full);
		if (doc == null)
		{
			string ext = Path.GetExtension (path).ToLowerInvariant ();
			var editor = new TextEditor
			{
				Text = File.ReadAllText (path), ShowLineNumbers = true, FontFamily = new FontFamily ("DejaVu Sans Mono,Consolas"), FontSize = 13,
				SyntaxHighlighting = HighlightingManager.Instance.GetDefinitionByExtension (ext is ".axaml" or ".ui" or ".csproj" ? ".xml" : ext),
			};
			var tab = new TabItem ();
			doc = new Document { Path = path, Editor = editor, Tab = tab };
			var made = doc;
			var title = new TextBlock { Text = doc.Name, VerticalAlignment = VerticalAlignment.Center };
			var close = new Button { Content = "×", Classes = { "tool" }, Padding = new Thickness (5, 0), FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
			ToolTip.SetTip (close, "Fermer");
			close.Click += (_, _) => CloseDocument (made);
			tab.Header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Children = { title, close } };
			made.Title = title;
			editor.TextArea.Caret.PositionChanged += (_, _) => ShowPosition ();
			if (doc.IsXaml)
			{
				made.Surface = new DesignSurface ();
				made.Surface.Picked += id => Pick (made, id);
				var split = new Grid { RowDefinitions = new RowDefinitions ("3*,4,2*") };
				Grid.SetRow (made.Surface, 0);
				var bar = new GridSplitter { ResizeDirection = GridResizeDirection.Rows };
				Grid.SetRow (bar, 1);
				Grid.SetRow (editor, 2);
				split.Children.Add (made.Surface);
				split.Children.Add (bar);
				split.Children.Add (editor);
				tab.Content = split;
				editor.TextArea.Caret.PositionChanged += (_, _) =>
				{
					if (!editor.IsKeyboardFocusWithin || _applying || XamlText.Parse (editor.Text) is not { } x)
						return;
					if (XamlText.ElementAtLine (x, editor.TextArea.Caret.Line) is { } e)
						Pick (made, XamlText.Elements (x).IndexOf (e), fromCode: true);
				};
			}
			else
				tab.Content = editor;
			editor.TextChanged += (_, _) =>
			{
				SetDirty (made, true);
				if (made.IsXaml && !_applying)
				{
					_preview.Stop ();
					_preview.Start ();
				}
			};
			_docs.Add (doc);
			Tabs.Items.Add (tab);
			UpdateCommands ();
		}
		Tabs.SelectedItem = doc.Tab;
	}

	void CloseDocument (Document d)
	{
		Save (d);
		_docs.Remove (d);
		Tabs.Items.Remove (d.Tab);
		UpdateCommands ();
	}

	void ShowPosition ()
	{
		var c = Current?.Editor.TextArea.Caret;
		Position.Text = c == null ? "" : "Ligne " + c.Line + ", colonne " + c.Column;
	}

	static void SetDirty (Document d, bool dirty)
	{
		d.Dirty = dirty;
		if (d.Title != null)
			d.Title.Text = d.Name + (dirty ? " ●" : "");
	}

	static void Save (Document d)
	{
		if (d.Dirty)
			File.WriteAllText (d.Path, d.Editor.Text, new UTF8Encoding (false));
		SetDirty (d, false);
	}

	void SaveAll ()
	{
		foreach (var d in _docs)
			Save (d);
		Status.Text = "Enregistré";
	}

	void DocumentChanged ()
	{
		ShowPosition ();
		RefreshPreview ();
		RefreshOutline ();
		ShowSelection ();
	}

	void RefreshPreview ()
	{
		if (Current is not { IsXaml: true, Surface: { } surface } d)
			return;
		try
		{
			var text = PreviewBuilder.Build (d.Editor.Text, _controls);
			if (text == null)
			{
				surface.Clear ();
				Status.Text = "Pas d'aperçu pour ce fichier";
				return;
			}
			surface.Show (text);
			surface.Select (d.Selected);
			if (Status.Text is null or "" || !_keepStatus)
				Status.Text = "Aperçu à jour";
			_keepStatus = false;
			RefreshOutline ();
			ShowSelection ();
		}
		catch (Exception e)
		{
			Status.Text = "Aperçu : " + e.Message.Split ('\n')[0];
		}
	}

	void RefreshOutline ()
	{
		if (Current is not { IsXaml: true } d || XamlText.Parse (d.Editor.Text) is not { } x)
		{
			Outline.ItemsSource = null;
			return;
		}
		var elements = XamlText.Elements (x);
		TreeViewItem Node (XElement e)
		{
			string label = e.Name.LocalName + (e.Attribute ("Name")?.Value is { } n ? "  " + n : "");
			var item = new TreeViewItem { Header = label, Tag = elements.IndexOf (e), IsExpanded = true };
			foreach (var c in e.Elements ())
				item.Items.Add (Node (c));
			return item;
		}
		Outline.ItemsSource = new[] { Node (x.Root!) };
	}

	// --- la selection -------------------------------------------------------------------------

	void Pick (Document d, int id, bool fromCode = false)
	{
		if (d.Selected == id)
			return;
		d.Selected = id;
		d.Surface?.Select (id);
		ShowSelection ();
		if (!fromCode && XamlText.Parse (d.Editor.Text) is { } x && id >= 0 && id < XamlText.Elements (x).Count)
		{
			_applying = true;
			int line = ((System.Xml.IXmlLineInfo) XamlText.Elements (x)[id]).LineNumber;
			d.Editor.ScrollToLine (line);
			d.Editor.TextArea.Caret.Line = line;
			_applying = false;
		}
	}

	XElement? SelectedElement (XDocument x)
	{
		var list = XamlText.Elements (x);
		return Current is { Selected: >= 0 } d && d.Selected < list.Count ? list[d.Selected] : null;
	}

	void ShowSelection ()
	{
		XElement? element = null;
		if (Current is { IsXaml: true } d && XamlText.Parse (d.Editor.Text) is { } x)
			element = SelectedElement (x);
		var live = Current?.Surface?.Selected;
		_properties.Show (element, live);
		_events.Show (element, live);
	}

	/// <summary>Une edition du texte, d'une seule traite pour l'annulation ; l'apercu suit.</summary>
	void Apply (Document d, string text)
	{
		if (text == d.Editor.Text)
			return;
		_applying = true;
		d.Editor.Document.Replace (0, d.Editor.Document.TextLength, text);
		_applying = false;
		RefreshPreview ();
	}

	void Edit (Func<string, XElement, string> change, bool keep = true)
	{
		if (Current is not { IsXaml: true } d || XamlText.Parse (d.Editor.Text) is not { } x || SelectedElement (x) is not { } e)
			return;
		string text;
		try { text = change (d.Editor.Text, e); }
		catch (ArgumentException ex)
		{
			Status.Text = ex.Message;
			return;
		}
		if (!keep)
			d.Selected = -1;
		Apply (d, text);
	}

	void SetAttribute (string name, string? value) => Edit ((text, e) => XamlText.SetAttribute (text, e, name, value));

	void RemoveSelected ()
	{
		if (Current is { Selected: > 0 })
			Edit (XamlText.Remove, keep: false);
	}

	// --- la palette ---------------------------------------------------------------------------

	void OnReleased (object? sender, PointerReleasedEventArgs e)
	{
		var name = _dragged;
		_dragged = null;
		if (name == null || Current?.Surface is not { } surface || !surface.Contains (e.GetPosition (this), this))
			return;
		Insert (name, surface.HitAtWindow (e.GetPosition (this), this));
	}

	/// <summary>Peut-il recevoir un enfant de plus ?</summary>
	static bool Accepts (XElement e)
	{
		var type = PreviewBuilder.TypeOf (e);
		if (type == null && e.Name.LocalName != "Window")
			return false;
		var children = e.Elements ().Where (c => !c.Name.LocalName.Contains ('.'));
		if (type != null && typeof (Panel).IsAssignableFrom (type))
			return true;
		if (type != null && (typeof (ContentControl).IsAssignableFrom (type) || typeof (Decorator).IsAssignableFrom (type)))
			return !children.Any () && e.Attribute ("Content") == null;
		return false;
	}

	void Insert (string name, int target)
	{
		if (Current is not { IsXaml: true } d || XamlText.Parse (d.Editor.Text) is not { } x)
			return;
		var control = _controls.FirstOrDefault (c => c.Name == name);
		if (control != null && Path.GetFullPath (control.Path) == Path.GetFullPath (d.Path))
		{
			Status.Text = "Un contrôle ne peut pas se contenir lui-même";
			return;
		}
		string text = d.Editor.Text, snippet = ControlCatalog.Snippet (name);
		if (control != null)
		{
			string ns = "using:" + control.Namespace, prefix = XamlText.PrefixOf (x, ns) ?? "local";
			text = XamlText.EnsureNamespace (text, x, prefix, ns);
			x = XamlText.Parse (text)!;
			snippet = "<" + prefix + ":" + name + " />";
		}
		var list = XamlText.Elements (x);
		var parent = target >= 0 && target < list.Count ? list[target] : x.Root!;
		while (parent != null && !Accepts (parent))
			parent = parent.Parent;
		parent ??= x.Root!.Elements ().FirstOrDefault (Accepts);
		if (parent == null)
		{
			Status.Text = "Pas de place ici : sélectionnez un panneau";
			return;
		}
		Status.Text = name + " ajouté dans " + parent.Name.LocalName;
		_keepStatus = true;
		Apply (d, XamlText.AddChild (text, parent, snippet));
	}

	// --- le code-behind -----------------------------------------------------------------------

	/// <summary>L'evenement relie a une methode du .axaml.cs, creee si besoin ; le curseur y va.</summary>
	void AddEventHandler (string eventName, string signature)
	{
		if (Current is not { IsXaml: true } d || XamlText.Parse (d.Editor.Text) is not { } x || SelectedElement (x) is not { } e)
			return;
		string? cls = x.Root!.Attribute (XNamespace.Get (ControlCatalog.X) + "Class")?.Value;
		string code = CodeBehind.CodeFile (d.Path);
		if (cls == null || !File.Exists (code))
		{
			Status.Text = "Pas de code-behind : il faut x:Class et " + Path.GetFileName (code);
			return;
		}
		string method = e.Attribute (eventName)?.Value ?? "On" + (e.Attribute ("Name")?.Value ?? e.Name.LocalName) + eventName;
		Apply (d, XamlText.SetAttribute (d.Editor.Text, e, eventName, method));
		Open (code);
		var cs = Current!;
		try
		{
			var (text, line) = CodeBehind.AddHandler (cs.Editor.Text, cls.Substring (cls.LastIndexOf ('.') + 1), method, signature);
			if (text != cs.Editor.Text)
				cs.Editor.Document.Replace (0, cs.Editor.Document.TextLength, text);
			GoTo (cs, line + 2, 3);
		}
		catch (ArgumentException ex)
		{
			Status.Text = ex.Message;
		}
	}

	// --- la construction ----------------------------------------------------------------------

	void Build (bool run)
	{
		if (_project == null || _running != null)
			return;
		SaveAll ();
		Errors.Items.Clear ();
		ErrorsTab.Header = "Erreurs";
		Output.Text = "";
		ShowLower (true);
		var project = _project;
		string name = project.AssemblyName;
		var info = OperatingSystem.IsWindows ()
			? new ProcessStartInfo ("dotnet", "build \"" + project.Path + "\" -c Release -v q")
			: new ProcessStartInfo ("/bin/pibuild", "\"" + project.Directory + "\" " + name);
		info.RedirectStandardOutput = info.RedirectStandardError = true;
		info.UseShellExecute = false;
		var p = new Process { StartInfo = info, EnableRaisingEvents = true };
		void Line (string? s)
		{
			if (s != null)
				Dispatcher.UIThread.Post (() => AddLine (s));
		}
		p.OutputDataReceived += (_, e) => Line (e.Data);
		p.ErrorDataReceived += (_, e) => Line (e.Data);
		p.Exited += (_, _) => Dispatcher.UIThread.Post (() =>
		{
			int code = p.ExitCode;
			_running = null;
			UpdateCommands ();
			Status.Text = code == 0 ? "Compilé" : "Échec de la compilation";
			if (code == 0 && run && !OperatingSystem.IsWindows ())
				Process.Start ("/apps/" + name + ".app/main");
		});
		Status.Text = "Compilation...";
		_running = p;
		UpdateCommands ();
		try
		{
			p.Start ();
			p.BeginOutputReadLine ();
			p.BeginErrorReadLine ();
		}
		catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException)
		{
			_running = null;
			UpdateCommands ();
			Status.Text = e.Message;
		}
	}

	void AddLine (string line)
	{
		if (line.StartsWith ("make: jobserver"))
			return;
		Output.Text += line + "\n";
		var m = s_error.Match (line);
		if (m.Success)
		{
			Errors.Items.Add (new BuildError (m.Groups[1].Value, int.Parse (m.Groups[2].Value), int.Parse (m.Groups[3].Value), m.Groups[4].Value));
			ErrorsTab.Header = "Erreurs (" + Errors.Items.Count + ")";
		}
	}

	void GoToError ()
	{
		if (Errors.SelectedItem is not BuildError e || _project == null)
			return;
		string path = Path.IsPathRooted (e.File) ? e.File : Path.Combine (_project.Directory, e.File);
		if (!File.Exists (path))
			return;
		Open (path);
		if (Current is { } d)
			GoTo (d, e.Line, e.Column);
	}

	/// <summary>Le curseur a cette ligne, une fois l'onglet affiche.</summary>
	static void GoTo (Document d, int line, int column)
	{
		Dispatcher.UIThread.Post (() =>
		{
			d.Editor.TextArea.Caret.Line = line;
			d.Editor.TextArea.Caret.Column = column;
			d.Editor.ScrollToLine (line);
			d.Editor.Focus ();
		}, DispatcherPriority.Loaded);
	}
}
