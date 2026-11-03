// Studio : ebauche d'IDE pour les applications .NET d'ARMPI (docs/DOTNET.md). Une seule fenetre : l'arbre
// du projet, un onglet par fichier ; un .ui montre son rendu en haut et son texte en bas, un .cs son texte en
// plein ; les proprietes du controle sous le curseur ; Compiler lance pibuild, ses erreurs menent a leur ligne.
// studio.txt, dans le projet : « type = lib » construit une bibliotheque au lieu d'une application.
//     /apps/studio.app/main [dossier du projet]
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using System.Windows.Forms.Markup;
using System.Xml.Linq;

class Document
{
	public string Path;
	public CodeEditor Editor;
	public bool IsUi => Path.EndsWith (".ui", StringComparison.OrdinalIgnoreCase);
	public bool Dirty;
}

class Studio : Form
{
	readonly TreeView tree = new () { Dock = DockStyle.Fill };
	readonly DocumentTabs tabs = new ();
	readonly Panel work = new () { Dock = DockStyle.Fill };
	readonly SplitContainer uiSplit = new () { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 300, Visible = false };
	readonly Panel preview = new () { Left = 12, Top = 12, Width = 400, Height = 240 };
	readonly TextBox output = new () { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true };
	readonly DataGridView errors = new () { Dock = DockStyle.Fill };
	readonly DataGridView props = new () { Dock = DockStyle.Fill };
	readonly TextBox propValue = new () { Dock = DockStyle.Bottom };
	readonly Button propApply = new () { Dock = DockStyle.Bottom, Height = 30, Text = "Appliquer" };
	readonly Label status = new () { Dock = DockStyle.Bottom, Height = 24, Text = "Pret" };
	readonly List<Document> docs = new ();
	readonly System.Windows.Forms.Timer refresh = new () { Interval = 500 };
	readonly Dictionary<TreeNode, string> paths = new ();
	readonly SplitContainer all = new () { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, Panel2MinSize = 100 };
	ToolStripMenuItem menuSave, menuAddLib, menuLib, menuBuild, menuRun, menuOutput;
	string project;
	bool library;
	Document current;
	Process running;
	XElement selected;

	Studio (string folder)
	{
		Text = "Studio";
		Width = 1000;
		Height = 640;
		WindowState = FormWindowState.Maximized;
		BuildMenu ();
		var tools = new ToolStrip ();
		tools.Items.Add (Tool ("+", "Nouveau", (s, e) => NewProject ()));
		tools.Items.Add (Tool ("⬆", "Ouvrir", (s, e) => OpenProject ()));
		tools.Items.Add (Tool ("⬇", "Enregistrer", (s, e) => SaveAll ()));
		tools.Items.Add (new ToolStripSeparator ());
		tools.Items.Add (Tool ("⚙", "Compiler", (s, e) => Build (false)));
		tools.Items.Add (Tool ("▶", "Executer", (s, e) => Build (true)));
		tools.Items.Add (new ToolStripSeparator ());
		tools.Items.Add (Tool ("≡", "Sortie", (s, e) => ShowOutput (all.Panel2Collapsed)));

		var center = new Panel { Dock = DockStyle.Fill };
		center.Controls.Add (tabs);
		center.Controls.Add (work);
		work.Controls.Add (uiSplit);
		uiSplit.Panel1.Controls.Add (preview);

		var side = new Panel { Dock = DockStyle.Fill };
		errors.Columns.Add (new DataGridViewColumn ("Fichier", 180));
		errors.Columns.Add (new DataGridViewColumn ("Ligne", 60));
		errors.Columns.Add (new DataGridViewColumn ("Code", 80));
		errors.Columns.Add (new DataGridViewColumn ("Message", 600));
		errors.RowActivated += (s, e) => GoToError ();
		props.Columns.Add (new DataGridViewColumn ("Propriete", 110));
		props.Columns.Add (new DataGridViewColumn ("Valeur", 150));
		props.SelectionChanged += (s, e) => { if (props.CurrentRow != null) propValue.Text = props.CurrentRow.Cells[1]?.ToString (); };
		propApply.Click += (s, e) => ApplyProperty ();
		side.Controls.Add (propApply);
		side.Controls.Add (propValue);
		side.Controls.Add (props);

		var right = new SplitContainer { Dock = DockStyle.Fill, Panel2MinSize = 200 };
		right.Panel1.Controls.Add (center);
		right.Panel2.Controls.Add (side);
		var main = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 200 };
		main.Panel1.Controls.Add (tree);
		main.Panel2.Controls.Add (right);
		var bottom = new SplitContainer { Dock = DockStyle.Fill };
		bottom.Panel1.Controls.Add (output);
		bottom.Panel2.Controls.Add (errors);
		all.Panel1.Controls.Add (main);
		all.Panel2.Controls.Add (bottom);

		Controls.Add (tools);
		Controls.Add (status);
		Controls.Add (all);
		void Place ()
		{
			if (!all.Panel2Collapsed)
				all.SplitterDistance = Height - 200;
			right.SplitterDistance = right.Width - 270;
			bottom.SplitterDistance = bottom.Width / 2;
		}
		ClientSizeChanged += (s, e) => Place ();
		Shown += (s, e) =>
		{
			ShowOutput (Height > 800);	// (1024x768 et moins : repliee, Compiler l'ouvre)
			Place ();
			UpdateMenus ();
			if (folder != null)
				LoadProject (folder);
		};

		tree.NodeActivated += (s, e) => { if (e.Node != null && paths.TryGetValue (e.Node, out var p)) Open (p); };
		tabs.SelectedIndexChanged += (s, e) => { if (tabs.SelectedIndex >= 0 && tabs.SelectedIndex < docs.Count) Show (docs[tabs.SelectedIndex]); };
		tabs.CloseRequested += (s, i) => CloseTab (i);
		refresh.Tick += (s, e) => { refresh.Stop (); RefreshPreview (); };
	}

	static ToolStripButton Tool (string glyph, string text, EventHandler click)
	{
		var b = new ToolStripButton (text) { Glyph = glyph };
		b.Click += click;
		return b;
	}

	void BuildMenu ()
	{
		ToolStripMenuItem Item (string text, Action a, Keys keys = Keys.None) => new (text, null, (s, e) => a (), keys);
		var file = new ToolStripMenuItem ("Fichier");
		file.DropDownItems.Add (Item ("Nouveau projet", NewProject, Keys.Control | Keys.N));
		file.DropDownItems.Add (Item ("Nouvelle bibliotheque", NewLibrary));
		file.DropDownItems.Add (Item ("Ouvrir un projet", OpenProject, Keys.Control | Keys.O));
		file.DropDownItems.Add (new ToolStripSeparator ());
		file.DropDownItems.Add (menuSave = Item ("Enregistrer tout", SaveAll, Keys.Control | Keys.S));
		var edit = new ToolStripMenuItem ("Edition");
		edit.DropDownItems.Add (Item ("Annuler", () => current?.Editor.Undo (), Keys.Control | Keys.Z));
		edit.DropDownItems.Add (Item ("Retablir", () => current?.Editor.Redo (), Keys.Control | Keys.Y));
		edit.DropDownItems.Add (new ToolStripSeparator ());
		edit.DropDownItems.Add (Item ("Couper", () => current?.Editor.Cut (), Keys.Control | Keys.X));
		edit.DropDownItems.Add (Item ("Copier", () => current?.Editor.Copy (), Keys.Control | Keys.C));
		edit.DropDownItems.Add (Item ("Coller", () => current?.Editor.Paste (), Keys.Control | Keys.V));
		var view = new ToolStripMenuItem ("Affichage");
		view.DropDownItems.Add (menuOutput = Item ("Sortie de la compilation", () => ShowOutput (all.Panel2Collapsed), Keys.F4));
		var proj = new ToolStripMenuItem ("Projet");
		proj.DropDownItems.Add (menuBuild = Item ("Compiler", () => Build (false), Keys.F6));
		proj.DropDownItems.Add (menuRun = Item ("Executer", () => Build (true), Keys.F5));
		proj.DropDownItems.Add (new ToolStripSeparator ());
		proj.DropDownItems.Add (menuAddLib = Item ("Ajouter une bibliotheque interne", AddLibrary));
		proj.DropDownItems.Add (menuLib = Item ("Construire une bibliotheque", () => SetLibrary (!library)));
		MainMenuStrip = new MenuStrip ();
		MainMenuStrip.Items.Add (file);
		MainMenuStrip.Items.Add (edit);
		MainMenuStrip.Items.Add (view);
		MainMenuStrip.Items.Add (proj);
	}

	void UpdateMenus ()
	{
		menuSave.Enabled = project != null;
		menuBuild.Enabled = project != null && running == null;
		menuRun.Enabled = project != null && running == null && !library;
		menuAddLib.Enabled = project != null && !library;
		menuLib.Enabled = project != null;
		menuLib.Checked = library;
		menuOutput.Checked = !all.Panel2Collapsed;
	}

	void ShowOutput (bool show)
	{
		all.Panel2Collapsed = !show;
		if (show)
			all.SplitterDistance = Height - 200;
		menuOutput.Checked = show;
	}

	string Ask (string title, string value)
	{
		var name = new TextBox { Text = value };
		var ok = new Button { Width = 90, Text = "OK", DialogResult = DialogResult.OK };
		var body = new StackPanel { Dock = DockStyle.Fill, Padding = 12, Spacing = 8 };
		body.Controls.Add (new Label { Text = title });
		body.Controls.Add (name);
		body.Controls.Add (ok);
		using var d = new Form { Text = title, Width = 320, Height = 150, FormBorderStyle = FormBorderStyle.FixedDialog, AcceptButton = ok };
		d.Controls.Add (body);
		if (d.ShowDialog () != DialogResult.OK || !Regex.IsMatch (name.Text, @"^[A-Za-z_][A-Za-z0-9_]*$"))
			return null;
		return name.Text;
	}

	static string LibrarySource (string name) => "namespace " + name + ";\n\npublic static class Library\n{\n"
		+ "\tpublic static string Hello () => \"Bonjour de " + name + "\";\n}\n";

	void NewLibrary ()
	{
		string name = Ask ("Nouvelle bibliotheque", "malib");
		if (name == null)
			return;
		string dir = "SD0:/projets/" + name;
		Directory.CreateDirectory (dir);
		File.WriteAllText (dir + "/" + name + ".cs", LibrarySource (name));
		File.WriteAllText (dir + "/studio.txt", "type = lib\n");
		LoadProject (dir);
	}

	// lib/<nom> : une DLL a cote de l'application (docs/GCC.md)
	void AddLibrary ()
	{
		string name = Ask ("Bibliotheque interne", "malib");
		if (name == null)
			return;
		string dir = project + "/lib/" + name;
		Directory.CreateDirectory (dir);
		File.WriteAllText (dir + "/" + name + ".cs", LibrarySource (name));
		LoadProject (project);
	}

	void SetLibrary (bool on)
	{
		library = on;
		string file = project + "/studio.txt";
		if (on)
			File.WriteAllText (file, "type = lib\n");
		else if (File.Exists (file))
			File.Delete (file);
		LoadProject (project);
	}

	void NewProject ()
	{
		string root = "SD0:/projets", name = "app" + (DateTime.Now.Ticks % 10000);
		string dir = root + "/" + name;
		Directory.CreateDirectory (dir);
		File.WriteAllText (dir + "/MainForm.ui", "<Form Text=\"" + name + "\" Width=\"420\" Height=\"240\">\n"
			+ "  <StackPanel Dock=\"Fill\" Padding=\"12\" Spacing=\"8\">\n"
			+ "    <Label Text=\"Bonjour\"/>\n"
			+ "    <Button Text=\"Cliquer\" Width=\"100\" Click=\"OnCliquer\"/>\n"
			+ "  </StackPanel>\n</Form>\n");
		File.WriteAllText (dir + "/MainForm.cs", "using System;\nusing System.Windows.Forms;\n\npartial class MainForm\n{\n"
			+ "\tpublic MainForm () { InitializeComponent (); }\n\n"
			+ "\tvoid OnCliquer (object sender, EventArgs e) => MessageBox.Show (\"Bonjour\");\n\n"
			+ "\tstatic void Main () => Application.Run (new MainForm ());\n}\n");
		LoadProject (dir);
	}

	void OpenProject ()
	{
		using var d = new FolderBrowserDialog ();
		if (d.ShowDialog () == DialogResult.OK)
			LoadProject (d.SelectedPath);
	}

	void LoadProject (string dir)
	{
		dir = dir.TrimEnd ('/');
		bool other = dir != project;
		if (other)
			while (docs.Count > 0)
				CloseTab (docs.Count - 1);
		project = dir;
		tree.Clear ();
		paths.Clear ();
		library = File.Exists (project + "/studio.txt") && Regex.IsMatch (File.ReadAllText (project + "/studio.txt"), @"^\s*type\s*=\s*lib\s*$", RegexOptions.Multiline);
		var root = tree.Nodes.Add (Path.GetFileName (project));
		Fill (root, project);
		root.Expand ();
		status.Text = project + (library ? " (bibliotheque)" : "");
		UpdateMenus ();
		var first = Directory.GetFiles (project, "*.ui").FirstOrDefault () ?? Directory.GetFiles (project, "*.cs").FirstOrDefault ();
		if (other && first != null)
			Open (first);
	}

	void Fill (TreeNode node, string dir)
	{
		foreach (var d in Directory.GetDirectories (dir).OrderBy (x => x))
			Fill (node.Nodes.Add (Path.GetFileName (d)), d);
		foreach (var f in Directory.GetFiles (dir).OrderBy (x => x))
			paths[node.Nodes.Add (Path.GetFileName (f))] = f;
	}

	void Open (string path)
	{
		string ext = Path.GetExtension (path).ToLowerInvariant ();
		if (ext != ".ui" && ext != ".cs" && ext != ".txt" && ext != ".xml")
			return;
		int at = docs.FindIndex (d => d.Path == path);
		if (at >= 0)
		{
			tabs.SelectedIndex = at;
			Show (docs[at]);
			return;
		}
		var language = ext == ".cs" ? CodeLanguage.CSharp : ext == ".txt" ? CodeLanguage.None : CodeLanguage.Xml;
		var doc = new Document { Path = path, Editor = new CodeEditor { Dock = DockStyle.Fill, Language = language, Text = File.ReadAllText (path), Visible = false } };
		(doc.IsUi ? uiSplit.Panel2 : work).Controls.Add (doc.Editor);
		doc.Editor.TextChanged += (s, e) =>
		{
			doc.Dirty = true;
			if (doc == current && doc.IsUi)
			{
				refresh.Stop ();
				refresh.Start ();
			}
		};
		doc.Editor.Click += (s, e) => { };
		docs.Add (doc);
		tabs.Add (Path.GetFileName (path));
		tabs.SelectedIndex = docs.Count - 1;
		Show (doc);
	}

	void Show (Document d)
	{
		if (current != null)
			current.Editor.Visible = false;
		current = d;
		uiSplit.Visible = d.IsUi;
		d.Editor.Visible = true;
		if (!d.IsUi)
			props.Rows.Clear ();
		d.Editor.Focus ();
		if (d.IsUi)
			RefreshPreview ();
		status.Text = d.Path;
	}

	void CloseTab (int i)
	{
		var d = docs[i];
		Save (d);
		docs.RemoveAt (i);
		tabs.RemoveAt (i);
		(d.IsUi ? uiSplit.Panel2 : work).Controls.Remove (d.Editor);
		if (d == current)
			current = null;
		if (docs.Count > 0)
			Show (docs[Math.Min (i, docs.Count - 1)]);
		else
			uiSplit.Visible = false;
	}

	void Save (Document d)
	{
		if (d.Dirty)
			File.WriteAllText (d.Path, d.Editor.Text);
		d.Dirty = false;
	}

	void SaveAll ()
	{
		foreach (var d in docs)
			Save (d);
		status.Text = "Enregistre";
	}

	void RefreshPreview ()
	{
		if (current == null || !current.IsUi)
			return;
		preview.Controls.Clear ();
		try
		{
			UiLoader.Preview (preview, current.Editor.Text, Path.GetDirectoryName (current.Path));
			uiSplit.SplitterDistance = Math.Clamp (preview.Height + 32, 80, uiSplit.Height - 120);
			ShowProperties (XDocument.Parse (current.Editor.Text, LoadOptions.SetLineInfo));
			status.Text = "Apercu a jour";
		}
		catch (Exception e)
		{
			status.Text = "Apercu : " + e.Message;
		}
		Application.Redraw ();
	}

	void ShowProperties (XDocument x)
	{
		int line = current.Editor.Caret.Line + 1;
		selected = x.Descendants ().LastOrDefault (e => ((System.Xml.IXmlLineInfo) e).LineNumber <= line) ?? x.Root;
		props.Rows.Clear ();
		foreach (var a in selected.Attributes ())
			props.Rows.Add (a.Name.LocalName, a.Value);
	}

	void ApplyProperty ()
	{
		if (selected == null || props.CurrentRow == null || current == null)
			return;
		string name = props.CurrentRow.Cells[0].ToString (), old = props.CurrentRow.Cells[1].ToString ();
		var lines = current.Editor.Text.Split ('\n');
		int at = ((System.Xml.IXmlLineInfo) selected).LineNumber - 1;
		lines[at] = lines[at].Replace (name + "=\"" + old + "\"", name + "=\"" + propValue.Text + "\"");
		current.Editor.Text = string.Join ("\n", lines);
		current.Dirty = true;
		RefreshPreview ();
	}

	void Build (bool run)
	{
		if (project == null || running != null || (run && library))
			return;
		SaveAll ();
		ShowOutput (true);
		output.Text = "";
		errors.Rows.Clear ();
		string name = Path.GetFileName (project);
		var log = new System.Text.StringBuilder ();
		var p = new Process ();
		p.StartInfo = new ProcessStartInfo ("/bin/pibuild", (library ? "lib " : "") + "\"" + project + "\" " + name) { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
		void Line (string s)
		{
			if (s == null)
				return;
			BeginInvoke (() =>
			{
				log.AppendLine (s);
				output.Text = log.ToString ();
				var m = Regex.Match (s, @"^(.+)\((\d+),(\d+)\): (?:error|warning) (\w+): (.+)$");
				if (m.Success)
					errors.Rows.Add (m.Groups[1].Value, int.Parse (m.Groups[2].Value), m.Groups[4].Value, m.Groups[5].Value);
			});
		}
		p.OutputDataReceived += (s, e) => Line (e.Data);
		p.ErrorDataReceived += (s, e) => Line (e.Data);
		p.EnableRaisingEvents = true;
		p.Exited += (s, e) => BeginInvoke (() =>
		{
			int code = p.ExitCode;
			running = null;
			UpdateMenus ();
			status.Text = code != 0 ? "Echec de la compilation" : library ? "Installee : /dotnet/" + name + ".dll" : "Compile";
			if (code == 0 && run)
				Process.Start ("/apps/" + name + ".app/main");
		});
		status.Text = "Compilation...";
		running = p;
		UpdateMenus ();
		try
		{
			p.Start ();
			p.BeginOutputReadLine ();
			p.BeginErrorReadLine ();
		}
		catch (Exception e)
		{
			running = null;
			UpdateMenus ();
			status.Text = "pibuild : " + e.Message;
		}
	}

	void GoToError ()
	{
		var r = errors.CurrentRow;
		if (r == null)
			return;
		string f = Path.Combine (project, r.Cells[0].ToString ());
		if (!File.Exists (f))
			return;
		Open (f);
		int line = Convert.ToInt32 (r.Cells[1]) - 1;
		current.Editor.GoToLine (line);
		current.Editor.HighlightLine (line);
	}

	static void Main (string[] args)
	{
		Application.Run (new Studio (args.Length > 0 ? args[0] : null));
	}
}
