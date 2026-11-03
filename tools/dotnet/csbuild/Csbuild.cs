// csbuild : construit un projet .csproj sous ARMPI (docs/DOTNET.md) ; pibuild le lance quand le dossier en contient un.
// Le .csproj dit le type : Exe ou WinExe s'installe comme un programme, Library dans /dotnet pour tous les programmes.
// Une ProjectReference est construite d'abord, et sa DLL va a cote du programme. Roslyn tourne dans ce process : une seule
// mise en route de csc pour tout l'arbre des projets.
//     csbuild [--print] [--type app|bin|sys|svc|lib] dossier-ou-csproj [nom]
#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using Armpi.Project;

static class Program
{
	/// <summary>ARMPI_DOTNET : un autre /dotnet, pour essayer csbuild hors de l'OS.</summary>
	internal static readonly string Root = Environment.GetEnvironmentVariable ("ARMPI_DOTNET") ?? "/dotnet";
	static readonly HashSet<string> s_done = new ();
	static MethodInfo? s_csc;

	static int Main (string[] args)
	{
		bool print = false, install = true;
		string? type = null;
		AssemblyLoadContext.Default.Resolving += (c, name) =>
		{
			foreach (var dir in new[] { Root + "/roslyn", Root + "/avalonia" })
				if (File.Exists (dir + "/" + name.Name + ".dll"))
					return c.LoadFromAssemblyPath (dir + "/" + name.Name + ".dll");
			return null;
		};
		var rest = new List<string> ();
		for (int i = 0; i < args.Length; i++)
		{
			if (args[i] == "--print") print = true;
			else if (args[i] == "--no-install") install = false;
			else if (args[i] == "--type") type = args[++i];
			else rest.Add (args[i]);
		}
		if (rest.Count == 0)
		{
			Console.Error.WriteLine ("usage : csbuild [--print] [--type app|bin|sys|svc|lib] dossier-ou-csproj [nom]");
			return 2;
		}
		try
		{
			var p = CsProject.Load (rest[0]);
			if (print)
			{
				Print (p);
				return 0;
			}
			foreach (var w in p.Warnings)
				Console.Error.WriteLine ("csbuild : " + w);
			var libs = new List<string> ();
			string dll = Build (p, libs);
			if (install)
				Install (p, dll, libs, type, rest.Count > 1 ? rest[1] : p.AssemblyName);
			else
				Console.WriteLine (dll);
			return 0;
		}
		catch (BuildError e)
		{
			Console.Error.WriteLine ("csbuild : " + e.Message);
			return 1;
		}
		catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Xml.XmlException or InvalidDataException)
		{
			Console.Error.WriteLine ("csbuild : " + e.Message);
			return 1;
		}
	}

	sealed class BuildError (string message) : Exception (message);

	static void Print (CsProject p)
	{
		Console.WriteLine ("OutputType=" + p.OutputType);
		Console.WriteLine ("AssemblyName=" + p.AssemblyName);
		Console.WriteLine ("TargetFramework=" + p.TargetFramework);
		foreach (var f in p.Compile.OrderBy (x => x, StringComparer.Ordinal))
			Console.WriteLine ("Compile=" + f);
		foreach (var f in p.Compile.Where (f => p.LinkOf (f) != null).OrderBy (x => x, StringComparer.Ordinal))
			Console.WriteLine ("Link=" + f + "|" + p.LinkOf (f));
		foreach (var f in p.Folders)
			Console.WriteLine ("Folder=" + f);
		foreach (var r in p.ProjectReferences)
			Console.WriteLine ("ProjectReference=" + Path.GetFileName (r));
		foreach (var k in p.Packages)
			Console.WriteLine ("PackageReference=" + k.Id + " " + k.Version);
		foreach (var w in p.Warnings)
			Console.WriteLine ("Warning=" + w);
	}

	/// <summary>Le projet et ses references -> sa DLL dans /tmp/csbuild ; libs recoit les DLL a livrer avec lui.</summary>
	static string Build (CsProject p, List<string> libs)
	{
		string obj = "/tmp/csbuild/" + p.AssemblyName;
		string dll = obj + "/" + p.AssemblyName + ".dll";
		if (!s_done.Add (p.Path))
			return dll;
		Directory.CreateDirectory (obj);
		var refs = new List<string> ();
		foreach (var path in p.ProjectReferences)
		{
			if (!File.Exists (path))
				throw new BuildError (p.Name + " : projet reference introuvable : " + path);
			var other = CsProject.Load (path);
			if (!other.IsLibrary)
				throw new BuildError (p.Name + " : " + other.Name + " n'est pas une bibliotheque");
			string d = Build (other, libs);
			refs.Add (d);
			if (!libs.Contains (d))
				libs.Add (d);
		}
		foreach (var r in p.References)
			refs.Add (ResolveReference (p, r));
		foreach (var k in p.Packages)
			refs.AddRange (ResolvePackage (p, k));
		foreach (var u in Directory.Exists (Root + "/ref/user") ? Directory.GetFiles (Root + "/ref/user", "*.dll") : Array.Empty<string> ())
			if (!Path.GetFileNameWithoutExtension (u).Equals (p.AssemblyName, StringComparison.OrdinalIgnoreCase) && !refs.Any (x => Path.GetFileName (x).Equals (Path.GetFileName (u), StringComparison.OrdinalIgnoreCase)))
				refs.Add (u);

		var sources = p.Compile.Select (f => f).ToList ();
		foreach (var f in sources)
			if (!File.Exists (Path.Combine (p.Directory, f)))
				throw new BuildError (p.Name + " : fichier introuvable : " + f);
		if (p.ImplicitUsings)
		{
			string g = obj + "/GlobalUsings.g.cs";
			string usings = string.Concat (p.GlobalUsings.Select (u => "global using " + u + ";\n"));
			if (!File.Exists (g) || File.ReadAllText (g) != usings)		// (sa date compte pour UpToDate)
				File.WriteAllText (g, usings);
			sources.Add (g);
		}
		var extra = p.AvaloniaXaml.Concat (p.AvaloniaResources.Select (r => r.Path)).Select (f => Path.Combine (p.Directory, f));
		if (UpToDate (p, dll, sources.Select (s => Path.Combine (p.Directory, s)).Concat (extra), refs))
		{
			Console.WriteLine (p.Name + " : a jour");
			return dll;
		}
		// Avalonia : le XAML passe dans la DLL apres Roslyn
		bool xaml = p.IsAvalonia && !string.Equals (p.Property ("EnableAvaloniaXamlCompilation"), "false", StringComparison.OrdinalIgnoreCase)
			&& (p.AvaloniaXaml.Count > 0 || p.AvaloniaResources.Count > 0);
		string raw = xaml ? obj + "/raw/" + p.AssemblyName + ".dll" : dll;
		Directory.CreateDirectory (Path.GetDirectoryName (raw)!);
		var a = new List<string>
		{
			"-noconfig", "@" + Rsp (obj), "-analyzer:" + Root + "/roslyn/uigen.dll",
			"-target:" + (p.IsLibrary ? "library" : "exe"), "-out:" + raw,
			"-define:" + string.Join (";", p.Defines),
		};
		if (p.Nullable != null) a.Add ("-nullable:" + p.Nullable);
		if (p.LangVersion != null) a.Add ("-langversion:" + p.LangVersion);
		if (p.StartupObject != null) a.Add ("-main:" + p.StartupObject);
		if (p.TreatWarningsAsErrors) a.Add ("-warnaserror+");
		if (p.NoWarn != null) a.Add ("-nowarn:" + p.NoWarn.Replace (';', ','));
		a.AddRange (refs.Select (r => "-r:" + r));
		a.AddRange (p.UiFiles.Select (f => "-additionalfile:" + f));
		if (xaml)
		{
			a.Add ("-resource:" + AvaloniaBuild.Resources (p, obj) + ",!AvaloniaResources");
			a.Add ("-analyzer:" + AvaloniaBuild.Tools + "/Avalonia.Generators.dll");
			a.Add ("-analyzerconfig:" + AvaloniaBuild.GeneratorConfig (p, obj));
			a.AddRange (p.AvaloniaXaml.Select (f => "-additionalfile:" + f));
		}
		a.AddRange (sources);
		Console.WriteLine (p.Name + " : compilation (" + sources.Count + " fichiers)");
		string here = Directory.GetCurrentDirectory ();
		Directory.SetCurrentDirectory (p.Directory);		// (les erreurs gardent des chemins relatifs)
		int code;
		try { code = Csc (a.ToArray ()); }
		finally { Directory.SetCurrentDirectory (here); }
		if (code != 0)
			throw new BuildError (p.Name + " : echec de la compilation");
		if (xaml)
		{
			Console.WriteLine (p.Name + " : XAML (" + p.AvaloniaXaml.Count + " fichiers)");
			try { AvaloniaBuild.CompileXaml (p, raw, dll, RspReferences ().Concat (refs).ToArray ()); }
			catch (InvalidDataException e) { throw new BuildError (p.Name + " : " + e.Message); }
		}
		return dll;
	}

	/// <summary>Les options de csc.rsp ; un autre Root y remplace /dotnet.</summary>
	static string Rsp (string obj)
	{
		if (Root == "/dotnet")
			return Root + "/csc.rsp";
		string rsp = obj + "/csc.rsp";
		File.WriteAllText (rsp, File.ReadAllText (Root + "/csc.rsp").Replace ("/dotnet/", Root + "/"));
		return rsp;
	}

	static IEnumerable<string> RspReferences ()
	{
		foreach (var line in File.ReadAllLines (Root + "/csc.rsp"))
			if (line.StartsWith ("-r:"))
				yield return line.Substring (3).Replace ("/dotnet/", Root + "/");
	}

	static bool UpToDate (CsProject p, string dll, IEnumerable<string> sources, List<string> refs)
	{
		if (!File.Exists (dll))
			return false;
		var built = File.GetLastWriteTimeUtc (dll);
		var inputs = sources.Concat (p.UiFiles.Select (u => Path.Combine (p.Directory, u))).Concat (refs).Append (p.Path);
		return inputs.All (f => File.Exists (f) && File.GetLastWriteTimeUtc (f) <= built);
	}

	static string ResolveReference (CsProject p, string reference)
	{
		if (File.Exists (reference))
			return reference;
		string name = Path.GetFileNameWithoutExtension (reference);
		foreach (var dir in new[] { Root + "/ref/user", Root + "/ref" })
			if (File.Exists (dir + "/" + name + ".dll"))
				return dir + "/" + name + ".dll";
		throw new BuildError (p.Name + " : reference introuvable : " + reference);
	}

	/// <summary>Pas de NuGet sous ARMPI : un paquet doit deja etre dans /dotnet/ref.</summary>
	static IEnumerable<string> ResolvePackage (CsProject p, PackageRef k)
	{
		string dll = Root + "/ref/" + k.Id + ".dll";
		if (File.Exists (dll))
			return new[] { dll };
		string pack = Root + "/ref/packages/" + k.Id;
		if (Directory.Exists (pack))
			return Directory.GetFiles (pack, "*.dll");
		throw new BuildError (p.Name + " : le paquet " + k.Id + " n'est pas dans /dotnet/ref : pas de NuGet sous ARMPI");
	}

	/// <summary>Roslyn dans ce process : csc.dll et ses dependances sont dans /dotnet/roslyn.</summary>
	static int Csc (string[] args)
	{
		if (s_csc == null)
		{
			var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath (Root + "/roslyn/csc.dll");
			s_csc = asm.EntryPoint ?? throw new BuildError ("csc.dll sans point d'entree");
		}
		return (int) s_csc.Invoke (null, new object[] { args })!;
	}

	static void Install (CsProject p, string dll, List<string> libs, string? type, string name)
	{
		type = p.IsLibrary ? "lib" : type ?? p.InstallType ?? "app";
		switch (type)
		{
		case "lib":
			if (!p.IsLibrary)
				throw new BuildError (p.Name + " : --type lib demande OutputType Library");
			Directory.CreateDirectory (Root + "/ref/user");
			File.Copy (dll, Root + "/" + p.AssemblyName + ".dll", true);
			File.Copy (dll, Root + "/ref/user/" + p.AssemblyName + ".dll", true);
			foreach (var l in libs)
				File.Copy (l, Root + "/" + Path.GetFileName (l), true);
			Console.WriteLine ("installe : " + Root + "/" + p.AssemblyName + ".dll");
			break;
		case "app":
			string app = "/apps/" + name + ".app";
			Directory.CreateDirectory (app);
			File.Copy (dll, app + "/main", true);
			if (p.IsAvalonia)
				File.WriteAllText (app + "/main.skia", "skia");	// (AppKit : l'hote dotnet-skia)
			foreach (var l in libs)
				File.Copy (l, app + "/" + Path.GetFileName (l), true);
			string manifest = Path.Combine (p.Directory, "app.txt");
			if (File.Exists (manifest))
				File.Copy (manifest, app + "/app.txt", true);
			else if (!File.Exists (app + "/app.txt"))
				File.WriteAllText (app + "/app.txt", "name = " + name + "\n", new UTF8Encoding (false));
			string data = Path.Combine (p.Directory, "data");
			if (Directory.Exists (data))
				foreach (var f in Directory.GetFiles (data))
					File.Copy (f, app + "/" + Path.GetFileName (f), true);
			Console.WriteLine ("installe : " + app);
			break;
		case "bin":
		case "sys":
		case "svc":
			if (libs.Count > 0)
				throw new BuildError (p.Name + " : un programme " + type + " tient en un fichier : pas de ProjectReference");
			string target = type == "bin" ? "/bin/" + name : type == "sys" ? "SD0:/sys/" + name : "SD0:/sys/svc/" + name;
			File.Copy (dll, target, true);
			Console.WriteLine ("installe : " + target);
			break;
		default:
			throw new BuildError ("type inconnu : " + type);
		}
	}
}
