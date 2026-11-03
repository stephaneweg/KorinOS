// AvaloniaBuild.cs -- ce que le paquet Avalonia fait d'habitude par MSBuild (AvaloniaBuildTasks.targets), sans MSBuild : ses
// deux taches lancees ici, et la configuration de son generateur. Elles viennent du paquet, dans /dotnet/avalonia.
#nullable enable
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using Armpi.Project;
using Microsoft.Build.Framework;

static class AvaloniaBuild
{
	public static string Tools => Program.Root + "/avalonia";

	/// <summary>!AvaloniaResources : les .axaml et les ressources du projet, en un fichier.</summary>
	public static string Resources (CsProject p, string obj)
	{
		string output = obj + "/resources";
		var items = p.AvaloniaXaml.Select (f => new Item (f, ("Link", p.LinkOf (f) ?? ""))).Concat (p.AvaloniaResources.Select (r => new Item (r.Path, ("Link", r.Link ?? "")))).ToArray ();
		Run ("GenerateAvaloniaResourcesTask", ("Output", output), ("Root", p.Directory), ("Resources", items.Cast<ITaskItem> ().ToArray ()), ("ReportImportance", "low"));
		return output;
	}

	/// <summary>Ce que le generateur lit : ses reglages (Avalonia.Generators.props) et le genre de chaque .axaml.</summary>
	public static string GeneratorConfig (CsProject p, string obj)
	{
		var s = new StringBuilder ("is_global = true\n");
		void Property (string name, string fallback) => s.Append ("build_property.").Append (name).Append (" = ").Append (p.Property (name) ?? fallback).Append ('\n');
		Property ("AvaloniaNameGeneratorIsEnabled", "true");
		Property ("AvaloniaNameGeneratorBehavior", "InitializeComponent");
		Property ("AvaloniaNameGeneratorDefaultFieldModifier", "internal");
		Property ("AvaloniaNameGeneratorFilterByPath", "*");
		Property ("AvaloniaNameGeneratorFilterByNamespace", "*");
		Property ("AvaloniaNameGeneratorViewFileNamingStrategy", "NamespaceAndClassName");
		Property ("AvaloniaNameGeneratorAttachDevTools", "false");
		Property ("AvaloniaUseCompiledBindingsByDefault", "true");
		foreach (var f in p.AvaloniaXaml)
			s.Append ('[').Append (p.Directory).Append ('/').Append (f).Append ("]\nbuild_metadata.AdditionalFiles.SourceItemGroup = AvaloniaXaml\n");
		string path = obj + "/avalonia.globalconfig";
		File.WriteAllText (path, s.ToString ());
		return path;
	}

	/// <summary>Le XAML compile dans la DLL : raw -> output.</summary>
	public static void CompileXaml (CsProject p, string raw, string output, IEnumerable<string> references)
	{
		bool bindings = !string.Equals (p.Property ("AvaloniaUseCompiledBindingsByDefault"), "false", StringComparison.OrdinalIgnoreCase);
		Run ("CompileAvaloniaXamlTask", ("AssemblyFile", new Item (raw, ("AvaloniaCompileOutput", output))),
			("References", references.Select (r => (ITaskItem) new Item (r)).ToArray ()), ("ProjectDirectory", p.Directory),
			("DefaultCompileBindings", bindings), ("ReportImportance", "low"), ("AnalyzerConfigFiles", Array.Empty<ITaskItem> ()));
	}

	static Assembly? s_tasks;

	static void Run (string task, params (string Name, object Value)[] arguments)
	{
		s_tasks ??= AssemblyLoadContext.Default.LoadFromAssemblyPath (Tools + "/Avalonia.Build.Tasks.dll");
		var type = s_tasks.GetType ("Avalonia.Build.Tasks." + task) ?? throw new InvalidDataException ("tache absente : " + task);
		var t = (ITask) Activator.CreateInstance (type)!;
		var engine = new Engine ();
		t.BuildEngine = engine;
		foreach (var (name, value) in arguments)
			type.GetProperty (name)!.SetValue (t, value);
		if (!t.Execute () || engine.Errors > 0)
			throw new InvalidDataException (task + " : echec");
	}

	sealed class Item : ITaskItem
	{
		readonly Dictionary<string, string> _metadata = new ();

		public Item (string spec, params (string Key, string Value)[] metadata)
		{
			ItemSpec = spec;
			foreach (var (k, v) in metadata)
				_metadata[k] = v;
		}

		public string ItemSpec { get; set; }
		public ICollection MetadataNames => _metadata.Keys;
		public int MetadataCount => _metadata.Count;
		public string GetMetadata (string metadataName) => _metadata.TryGetValue (metadataName, out var v) ? v : "";
		public void SetMetadata (string metadataName, string metadataValue) => _metadata[metadataName] = metadataValue;
		public void RemoveMetadata (string metadataName) => _metadata.Remove (metadataName);
		public void CopyMetadataTo (ITaskItem destinationItem)
		{
			foreach (var (k, v) in _metadata)
				destinationItem.SetMetadata (k, v);
		}
		public IDictionary CloneCustomMetadata () => new Dictionary<string, string> (_metadata);
	}

	/// <summary>Les erreurs sortent comme celles de csc : le Studio les lit de la meme facon.</summary>
	sealed class Engine : IBuildEngine
	{
		public int Errors;
		public bool ContinueOnError => false;
		public int LineNumberOfTaskNode => 0;
		public int ColumnNumberOfTaskNode => 0;
		public string ProjectFileOfTaskNode => "";
		public bool BuildProjectFile (string projectFileName, string[] targetNames, IDictionary globalProperties, IDictionary targetOutputs) => false;
		public void LogCustomEvent (CustomBuildEventArgs e) { }
		public void LogMessageEvent (BuildMessageEventArgs e) { }

		public void LogErrorEvent (BuildErrorEventArgs e)
		{
			Errors++;
			Console.WriteLine (e.File + "(" + e.LineNumber + "," + e.ColumnNumber + "): error " + e.Code + ": " + e.Message);
		}

		public void LogWarningEvent (BuildWarningEventArgs e)
		{
			Console.WriteLine (e.File + "(" + e.LineNumber + "," + e.ColumnNumber + "): warning " + e.Code + ": " + e.Message);
		}
	}
}
