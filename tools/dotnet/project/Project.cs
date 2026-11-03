// Project.cs -- un .csproj de Visual Studio lu sous ARMPI, ou MSBuild ne tourne pas : ce qu'il faut pour
// construire un projet (docs/DOTNET.md), partage par csbuild et le Studio. Le meme fichier s'ouvre dans Visual Studio.
// Un sous-ensemble : proprietes, conditions simples, Compile, AdditionalFiles, Reference, ProjectReference,
// PackageReference, Directory.Build.props. Ce qui n'est pas compris va dans Warnings, jamais en silence.
#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml.Linq;

[assembly: System.Reflection.AssemblyVersion ("1.0.0.0")]

namespace Armpi.Project;

public sealed class PackageRef
{
	public string Id = "";
	public string Version = "";
}

public sealed class CsProject
{
	readonly Dictionary<string, string> _props = new (StringComparer.OrdinalIgnoreCase);
	readonly List<string> _compile = new (), _ui = new ();
	readonly List<string> _xaml = new ();
	readonly List<string> _folders = new ();
	readonly Dictionary<string, string> _links = new ();
	readonly List<(string Path, string? Link)> _resources = new ();
	readonly List<string> _references = new ();
	readonly List<string> _projectRefs = new ();
	readonly List<PackageRef> _packages = new ();
	readonly List<string> _warnings = new ();
	List<string>? _tree;

	public string Path { get; }
	public string Directory { get; }
	public string Name { get; }

	/// <summary>Sources et .ui, relatifs au dossier du projet, separes par /.</summary>
	public IReadOnlyList<string> Compile => _compile;
	public IReadOnlyList<string> UiFiles => _ui;
	/// <summary>Avalonia : les .axaml, et les ressources avec leur Link (AvaloniaBuildTasks.props).</summary>
	public IReadOnlyList<string> AvaloniaXaml => _xaml;
	public IReadOnlyList<(string Path, string? Link)> AvaloniaResources => _resources;
	/// <summary>Les dossiers vides que le projet declare (Folder).</summary>
	public IReadOnlyList<string> Folders => _folders;
	/// <summary>La place d'un fichier dans l'arbre du projet : Link, sinon son chemin ; hors du dossier, a la racine.</summary>
	public string Display (string file)
	{
		if (_links.TryGetValue (file, out var link))
			return link;
		return file.StartsWith ("../") || System.IO.Path.IsPathRooted (file) ? System.IO.Path.GetFileName (file) : file;
	}
	public string? LinkOf (string file) => _links.GetValueOrDefault (file);
	/// <summary>Tous les fichiers de l'arbre du projet.</summary>
	public IEnumerable<string> TreeFiles => _compile.Concat (_ui).Concat (_xaml).Distinct ();
	public bool IsAvalonia => _packages.Any (k => k.Id.Equals ("Avalonia", StringComparison.OrdinalIgnoreCase));
	/// <summary>Chemins des assemblys de Reference (HintPath, sinon le nom).</summary>
	public IReadOnlyList<string> References => _references;
	/// <summary>Chemins complets des .csproj references.</summary>
	public IReadOnlyList<string> ProjectReferences => _projectRefs;
	public IReadOnlyList<PackageRef> Packages => _packages;
	public IReadOnlyList<string> Warnings => _warnings;

	public string? Property (string name) => _props.TryGetValue (name, out var v) && v.Length > 0 ? v : null;
	bool True (string name) => string.Equals (Property (name), "true", StringComparison.OrdinalIgnoreCase);

	public string OutputType => Property ("OutputType") ?? "Library";
	public bool IsLibrary => !OutputType.Equals ("Exe", StringComparison.OrdinalIgnoreCase) && !OutputType.Equals ("WinExe", StringComparison.OrdinalIgnoreCase);
	public string AssemblyName => Property ("AssemblyName") ?? Name;
	public string TargetFramework => Property ("TargetFramework") ?? (Property ("TargetFrameworks")?.Split (';')[0] ?? "");
	/// <summary>Propriete propre a ARMPI, ignoree par Visual Studio : app, bin, sys ou svc.</summary>
	public string? InstallType => Property ("ArmpiType");
	public bool ImplicitUsings => Property ("ImplicitUsings") is { } v && (v.Equals ("enable", StringComparison.OrdinalIgnoreCase) || v.Equals ("true", StringComparison.OrdinalIgnoreCase));
	public bool UseWindowsForms => True ("UseWindowsForms");
	public string? Nullable => Property ("Nullable");
	public string? LangVersion => Property ("LangVersion");
	public string? StartupObject => Property ("StartupObject");
	public bool TreatWarningsAsErrors => True ("TreatWarningsAsErrors");
	public string? NoWarn => Property ("NoWarn");

	/// <summary>Les symboles de compilation, ceux du SDK compris (Release, net10.0).</summary>
	public IEnumerable<string> Defines
	{
		get
		{
			foreach (var d in (Property ("DefineConstants") ?? "").Split (';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
				yield return d;
			yield return "TRACE";
			yield return (Property ("Configuration") ?? "Release").ToUpperInvariant ();
			var m = Regex.Match (TargetFramework, @"^net(\d+)\.(\d+)");
			if (!m.Success)
				yield break;
			yield return "NET";
			yield return "NET" + m.Groups[1].Value + "_" + m.Groups[2].Value;
			yield return "NET" + m.Groups[1].Value + "_" + m.Groups[2].Value + "_OR_GREATER";
		}
	}

	/// <summary>Les usings que le SDK genere pour ImplicitUsings.</summary>
	public IEnumerable<string> GlobalUsings
	{
		get
		{
			if (!ImplicitUsings)
				yield break;
			yield return "System";
			yield return "System.Collections.Generic";
			yield return "System.IO";
			yield return "System.Linq";
			yield return "System.Net.Http";
			yield return "System.Threading";
			yield return "System.Threading.Tasks";
			if (UseWindowsForms)
			{
				yield return "System.Drawing";
				yield return "System.Windows.Forms";
			}
		}
	}

	CsProject (string path, string configuration)
	{
		Path = System.IO.Path.GetFullPath (path).Replace ('\\', '/');
		Directory = System.IO.Path.GetDirectoryName (Path)!.Replace ('\\', '/');
		Name = System.IO.Path.GetFileNameWithoutExtension (Path);
		_props["Configuration"] = configuration;
		_props["Platform"] = "AnyCPU";
		_props["MSBuildProjectName"] = Name;
		_props["MSBuildProjectFile"] = System.IO.Path.GetFileName (Path);
		_props["MSBuildProjectDirectory"] = Directory;
		_props["MSBuildThisFileDirectory"] = Directory + "/";
	}

	/// <summary>Le .csproj de ce fichier, ou le seul .csproj de ce dossier.</summary>
	public static CsProject Load (string path, string configuration = "Release")
	{
		if (System.IO.Directory.Exists (path))
		{
			var all = System.IO.Directory.GetFiles (path, "*.csproj");
			if (all.Length != 1)
				throw new InvalidDataException (path + " : " + all.Length + " .csproj, il en faut un");
			path = all[0];
		}
		var p = new CsProject (path, configuration);
		p.Evaluate ();
		return p;
	}

	/// <summary>Le .csproj d'un dossier, s'il y en a un.</summary>
	public static string? Find (string directory) => System.IO.Directory.GetFiles (directory, "*.csproj").FirstOrDefault ();

	void Evaluate ()
	{
		for (var d = new DirectoryInfo (Directory); d != null; d = d.Parent)
		{
			var props = System.IO.Path.Combine (d.FullName, "Directory.Build.props");
			if (File.Exists (props))
			{
				Walk (XDocument.Load (props).Root!, d.FullName.Replace ('\\', '/') + "/", false);
				break;
			}
		}
		var root = XDocument.Load (Path).Root!;
		if (root.Attribute ("Sdk") == null)
			_warnings.Add ("pas d'attribut Sdk : ancien format de projet, non gere");
		Walk (root, Directory + "/", true);
		if (Property ("EnableDefaultItems")?.ToLowerInvariant () != "false" && Property ("EnableDefaultCompileItems")?.ToLowerInvariant () != "false")
			_compile.InsertRange (0, Glob ("**/*.cs").Where (f => !_compile.Contains (f) && !_removed.Contains (f)));
		_compile.RemoveAll (_removed.Contains);
		_ui.AddRange (Glob ("**/*.ui").Where (f => !_ui.Contains (f)));
		if (IsAvalonia && Property ("EnableDefaultItems")?.ToLowerInvariant () != "false")
			_xaml.AddRange (Glob ("**/*.axaml").Concat (Glob ("**/*.paml")).Where (f => !_xaml.Contains (f)));
	}

	readonly HashSet<string> _removed = new ();

	void Walk (XElement root, string here, bool main)
	{
		foreach (var e in root.Elements ())
		{
			_props["MSBuildThisFileDirectory"] = here;
			if (!Condition (e.Attribute ("Condition")?.Value))
				continue;
			switch (e.Name.LocalName)
			{
			case "PropertyGroup":
				foreach (var p in e.Elements ())
					if (Condition (p.Attribute ("Condition")?.Value))
						_props[p.Name.LocalName] = Expand (p.Value.Trim ());
				break;
			case "ItemGroup":
				foreach (var i in e.Elements ())
					if (Condition (i.Attribute ("Condition")?.Value))
						Item (i);
				break;
			case "Import":
			case "Target":
			case "UsingTask":
				_warnings.Add (e.Name.LocalName + " ignore : " + (e.Attribute ("Project") ?? e.Attribute ("Name"))?.Value);
				break;
			}
		}
	}

	void Item (XElement i)
	{
		string include = Expand (i.Attribute ("Include")?.Value ?? ""), remove = Expand (i.Attribute ("Remove")?.Value ?? "");
		string update = Expand (i.Attribute ("Update")?.Value ?? "");
		switch (i.Name.LocalName)
		{
		case "Compile":
			foreach (var r in Items (remove))
				_removed.Add (r);
			var excluded = new HashSet<string> (Items (Expand (i.Attribute ("Exclude")?.Value ?? "")));
			foreach (var f in Items (include))
				if (!excluded.Contains (f))
				{
					if (!_compile.Contains (f))
						_compile.Add (f);
					SetLink (i, f, include);
				}
			foreach (var f in Items (update))
				SetLink (i, f, update);
			break;
		case "Folder":
			foreach (var p in include.Split (';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
				_folders.Add (p.Replace ('\\', '/').TrimEnd ('/'));
			break;
		case "AdditionalFiles":
			foreach (var f in Items (include))
				if (f.EndsWith (".ui", StringComparison.OrdinalIgnoreCase) && !_ui.Contains (f))
					_ui.Add (f);
			break;
		case "AvaloniaXaml":
			foreach (var f in Items (include))
			{
				if (!_xaml.Contains (f))
					_xaml.Add (f);
				SetLink (i, f, include);
			}
			foreach (var f in Items (update))
				SetLink (i, f, update);
			break;
		case "AvaloniaResource":
			var link = i.Attribute ("Link")?.Value ?? i.Element (i.Name.Namespace + "Link")?.Value;
			foreach (var f in Items (include))
				_resources.Add ((f, link));
			break;
		case "Reference":
			var hint = i.Element (i.Name.Namespace + "HintPath")?.Value;
			_references.Add (hint != null ? Absolute (Expand (hint.Trim ())) : include.Split (',')[0].Trim ());
			break;
		case "ProjectReference":
			foreach (var p in include.Split (';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
				_projectRefs.Add (Absolute (p));
			break;
		case "PackageReference":
			_packages.Add (new PackageRef { Id = include, Version = i.Attribute ("Version")?.Value ?? i.Element (i.Name.Namespace + "Version")?.Value ?? "" });
			break;
		}
	}

	static string? Metadata (XElement i, string name) => i.Attribute (name)?.Value ?? i.Element (i.Name.Namespace + name)?.Value;

	/// <summary>Link, ou LinkBase avec le sous-dossier du motif : ou le fichier parait dans l'arbre.</summary>
	void SetLink (XElement i, string file, string spec)
	{
		string name = System.IO.Path.GetFileName (file);
		string recursive = "";
		var pattern = spec.Split (';').Select (s => s.Trim ().Replace ('\\', '/')).FirstOrDefault (s => s.Contains ("**"));
		if (pattern != null)
		{
			string fixedPart = pattern.Substring (0, pattern.IndexOf ("**", StringComparison.Ordinal));
			string dir = System.IO.Path.GetDirectoryName (file)?.Replace ('\\', '/') ?? "";
			if ((dir + "/").StartsWith (fixedPart, StringComparison.OrdinalIgnoreCase) && dir.Length + 1 > fixedPart.Length)
				recursive = dir.Substring (fixedPart.Length) + "/";
		}
		string? link = Metadata (i, "Link");
		if (link != null)
		{
			link = Expand (link).Replace ("%(Filename)", System.IO.Path.GetFileNameWithoutExtension (file))
				.Replace ("%(Extension)", System.IO.Path.GetExtension (file)).Replace ("%(RecursiveDir)", recursive);
			_links[file] = link.Replace ('\\', '/');
		}
		else if (Metadata (i, "LinkBase") is { } b)
			_links[file] = Expand (b).Replace ('\\', '/').TrimEnd ('/') + "/" + recursive + name;
	}

	string Absolute (string relative) => System.IO.Path.GetFullPath (System.IO.Path.Combine (Directory, relative.Replace ('\\', '/'))).Replace ('\\', '/');

	string Expand (string s) => Regex.Replace (s, @"\$\((\w+)\)", m => _props.TryGetValue (m.Groups[1].Value, out var v) ? v : Environment.GetEnvironmentVariable (m.Groups[1].Value) ?? "");

	// --- les fichiers ------------------------------------------------------------------------

	IEnumerable<string> Items (string spec)
	{
		foreach (var part in spec.Split (';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
		{
			var p = part.Replace ('\\', '/');
			if (p.StartsWith ("../") && (p.Contains ('*') || p.Contains ('?')))
				foreach (var f in GlobOutside (p))
					yield return f;
			else if (p.Contains ('*') || p.Contains ('?'))
				foreach (var f in Glob (p))
					yield return f;
			else
				yield return p.StartsWith ("./") ? p.Substring (2) : p;
		}
	}

	List<string> Tree ()
	{
		if (_tree != null)
			return _tree;
		_tree = new List<string> ();
		void Visit (string dir, string prefix)
		{
			foreach (var f in System.IO.Directory.GetFiles (dir).OrderBy (x => x, StringComparer.Ordinal))
				_tree.Add (prefix + System.IO.Path.GetFileName (f));
			foreach (var d in System.IO.Directory.GetDirectories (dir).OrderBy (x => x, StringComparer.Ordinal))
			{
				string n = System.IO.Path.GetFileName (d);
				// (le SDK : DefaultItemExcludes)
				if (prefix.Length == 0 && (n.Equals ("bin", StringComparison.OrdinalIgnoreCase) || n.Equals ("obj", StringComparison.OrdinalIgnoreCase)))
					continue;
				if (n.StartsWith ('.'))
					continue;
				Visit (d, prefix + n + "/");
			}
		}
		Visit (Directory, "");
		return _tree;
	}

	/// <summary>Un motif qui sort du dossier du projet : ../Commun/**/*.cs.</summary>
	IEnumerable<string> GlobOutside (string pattern)
	{
		int star = pattern.IndexOfAny (new[] { '*', '?' });
		int cut = pattern.LastIndexOf ('/', star);
		string prefix = pattern.Substring (0, cut + 1), rest = pattern.Substring (cut + 1);
		string dir = Absolute (prefix);
		if (!System.IO.Directory.Exists (dir))
			yield break;
		string r = "^" + Regex.Escape (rest).Replace (@"\*\*/", "(?:.*/)?").Replace (@"\*", "[^/]*").Replace (@"\?", "[^/]") + "$";
		var re = new Regex (r, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
		var all = System.IO.Directory.GetFiles (dir, "*", rest.Contains ("**") ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
		foreach (var f in all.OrderBy (x => x, StringComparer.Ordinal))
		{
			string sub = System.IO.Path.GetRelativePath (dir, f).Replace ('\\', '/');
			if (re.IsMatch (sub))
				yield return prefix + sub;
		}
	}

	IEnumerable<string> Glob (string pattern)
	{
		string r = "^" + Regex.Escape (pattern).Replace (@"\*\*/", "(?:.*/)?").Replace (@"\*", "[^/]*").Replace (@"\?", "[^/]") + "$";
		var re = new Regex (r, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
		return Tree ().Where (f => re.IsMatch (f));
	}

	// --- les conditions : 'a' == 'b', != , And, Or, !, Exists('x'), parentheses --------------

	static readonly Regex s_token = new (@"\s*(?:(\()|(\))|(==|!=)|(!)|'([^']*)'|\b(And|Or|Exists|true|false)\b)", RegexOptions.IgnoreCase);

	bool Condition (string? text)
	{
		if (string.IsNullOrWhiteSpace (text))
			return true;
		var tokens = new List<Match> ();
		string s = Expand (text);
		int at = 0;
		for (;;)
		{
			if (s.Substring (at).Trim ().Length == 0)
				break;
			var m = s_token.Match (s, at);
			if (!m.Success || m.Index != at)
			{
				_warnings.Add ("condition non comprise, tenue pour fausse : " + text);
				return false;
			}
			tokens.Add (m);
			at += m.Length;
		}
		int i = 0;
		try
		{
			bool r = Or (tokens, ref i);
			if (i == tokens.Count)
				return r;
		}
		catch (ArgumentOutOfRangeException) { }
		_warnings.Add ("condition non comprise, tenue pour fausse : " + text);
		return false;
	}

	bool Or (List<Match> t, ref int i)
	{
		bool r = And (t, ref i);
		while (i < t.Count && t[i].Value.Trim ().Equals ("or", StringComparison.OrdinalIgnoreCase))
		{
			i++;
			r |= And (t, ref i);
		}
		return r;
	}

	bool And (List<Match> t, ref int i)
	{
		bool r = Term (t, ref i);
		while (i < t.Count && t[i].Value.Trim ().Equals ("and", StringComparison.OrdinalIgnoreCase))
		{
			i++;
			r &= Term (t, ref i);
		}
		return r;
	}

	bool Term (List<Match> t, ref int i)
	{
		var m = t[i++];
		if (m.Groups[4].Success)
			return !Term (t, ref i);
		if (m.Groups[1].Success)
		{
			bool r = Or (t, ref i);
			i++;
			return r;
		}
		string word = m.Groups[6].Value;
		if (word.Equals ("true", StringComparison.OrdinalIgnoreCase))
			return true;
		if (word.Equals ("false", StringComparison.OrdinalIgnoreCase))
			return false;
		if (word.Equals ("Exists", StringComparison.OrdinalIgnoreCase))
		{
			i++;
			string path = Absolute (t[i++].Groups[5].Value);
			i++;
			return File.Exists (path) || System.IO.Directory.Exists (path);
		}
		string left = m.Groups[5].Value;
		string op = t[i++].Value.Trim ();
		string right = t[i++].Groups[5].Value;
		bool same = left.Equals (right, StringComparison.OrdinalIgnoreCase);
		return op == "==" ? same : !same;
	}
}
