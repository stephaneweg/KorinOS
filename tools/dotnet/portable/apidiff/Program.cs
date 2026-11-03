// apidiff -- ecart binaire entre notre System.Windows.Forms et celui de Windows (docs/WINFORMS-PORTABLE.md) :
//     apidiff surface <dossier des refs Windows> <dossier des refs du framework> <dossier de nos DLL> <sortie.tsv>
//     apidiff check <dossier de nos DLL> <dossier des refs du framework> <app.dll>...
// surface compare les membres publics et proteges type par type ; un membre compte si son type declarant et
// sa signature sont les memes, parce que c'est ce qu'un appel compile porte dans son MemberRef.
// check fait le travail du chargeur sur un binaire compile : chaque TypeRef et MemberRef vers WinForms ou
// System.Drawing doit se resoudre dans nos DLL, en remontant les classes de base comme le CLR.
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

static class Program
{
	static readonly string[] Spaces = { "System.Windows.Forms", "System.Drawing" };
	const BindingFlags Decl = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;

	static bool InScope (Type t) => t.Namespace != null && Spaces.Any (s => t.Namespace == s || t.Namespace.StartsWith (s + "."));
	static bool Visible (Type t) => t.IsPublic || t.IsNestedPublic || t.IsNestedFamily || t.IsNestedFamORAssem;
	static bool Visible (MethodBase m) => m.IsPublic || m.IsFamily || m.IsFamilyOrAssembly;
	static bool Visible (FieldInfo f) => f.IsPublic || f.IsFamily || f.IsFamilyOrAssembly;

	static string Name (Type t) => t.IsGenericParameter ? t.Name : t.HasElementType ? Name (t.GetElementType ()) + (t.IsArray ? "[]" : t.IsByRef ? "&" : "*")
		: t.IsGenericType ? t.GetGenericTypeDefinition ().FullName + "<" + string.Join (",", t.GetGenericArguments ().Select (Name)) + ">"
		: t.FullName ?? t.Name;

	static string Sig (MethodBase m) => m.Name + "(" + string.Join (",", m.GetParameters ().Select (p => Name (p.ParameterType))) + ")"
		+ (m is MethodInfo i ? ":" + Name (i.ReturnType) : "");

	static IEnumerable<string> Members (Type t)
	{
		foreach (var m in t.GetMethods (Decl).Where (Visible))
			yield return Sig (m);
		foreach (var c in t.GetConstructors (Decl).Where (Visible))
			yield return Sig (c);
		foreach (var f in t.GetFields (Decl).Where (Visible))
			if (!f.IsSpecialName)
				yield return f.Name + ":" + Name (f.FieldType);
	}

	static MetadataLoadContext Context (IEnumerable<string> dirs)
	{
		var seen = new Dictionary<string, string> (StringComparer.OrdinalIgnoreCase);
		foreach (var d in dirs)
			foreach (var f in Directory.GetFiles (d, "*.dll"))
				seen.TryAdd (Path.GetFileName (f), f);
		return new MetadataLoadContext (new PathAssemblyResolver (seen.Values), "System.Runtime");
	}

	static Dictionary<string, Type> Types (MetadataLoadContext c, IEnumerable<string> files)
	{
		var all = new Dictionary<string, Type> ();
		foreach (var f in files)
		{
			Type[] ts;
			try { ts = c.LoadFromAssemblyPath (f).GetTypes (); }
			catch (ReflectionTypeLoadException e) { ts = e.Types.Where (t => t != null).ToArray (); }
			foreach (var t in ts)
				if (InScope (t) && Visible (t))
					all[t.FullName] = t;
		}
		return all;
	}

	static int Surface (string[] a)
	{
		string win = a[1], core = a[2], ours = a[3], tsv = a[4];
		using var cw = Context (new[] { win, core });
		using var co = Context (new[] { ours, core });
		var wt = Types (cw, new[] { "System.Windows.Forms", "System.Windows.Forms.Primitives", "System.Drawing.Common" }.Select (n => win + "/" + n + ".dll"));
		var ot = Types (co, Directory.GetFiles (ours, "System.Windows.Forms*.dll").Concat (Directory.GetFiles (ours, "System.Drawing.Common.dll")));
		int same = 0, moved = 0, sigdiff = 0, extra = 0, typeOnlyOurs = 0, baseDiff = 0, typeShared = 0;
		using var o = new StreamWriter (tsv);
		o.WriteLine ("etat\ttype\tmembre\tdetail");
		foreach (var t in ot.Values.OrderBy (t => t.FullName))
		{
			if (!wt.TryGetValue (t.FullName, out var w))
			{
				typeOnlyOurs++;
				o.WriteLine ($"type-armpi\t{t.FullName}\t\t{t.BaseType?.FullName}");
				continue;
			}
			typeShared++;
			if (w.BaseType?.FullName != t.BaseType?.FullName)
			{
				baseDiff++;
				o.WriteLine ($"base\t{t.FullName}\t\tarmpi {t.BaseType?.FullName} / windows {w.BaseType?.FullName}");
			}
			var wm = new HashSet<string> (Members (w));
			var chain = new HashSet<string> ();
			for (var b = w; b != null && b.FullName != "System.Object"; b = b.BaseType)
				foreach (var m in Members (b))
					chain.Add (m);
			var names = wm.Select (m => m.Split ('(', ':')[0]).ToHashSet ();
			foreach (var m in Members (t).OrderBy (x => x))
			{
				string shortName = m.Split ('(', ':')[0];
				if (wm.Contains (m))
					same++;
				else if (chain.Contains (m))
				{
					moved++;
					o.WriteLine ($"ailleurs\t{t.FullName}\t{m}\tdeclare sur une base a Windows");
				}
				else if (names.Contains (shortName))
				{
					sigdiff++;
					o.WriteLine ($"signature\t{t.FullName}\t{m}\t{string.Join (" | ", wm.Where (x => x.Split ('(', ':')[0] == shortName).Take (3))}");
				}
				else
				{
					extra++;
					o.WriteLine ($"armpi\t{t.FullName}\t{m}\t");
				}
			}
		}
		int wall = wt.Values.Sum (t => Members (t).Count ());
		Console.WriteLine ($"types Windows {wt.Count}, nos types {ot.Count} : communs {typeShared}, dont base differente {baseDiff}, sans equivalent {typeOnlyOurs}");
		Console.WriteLine ($"membres Windows {wall} ; chez nous : identiques {same}, sur un autre type {moved}, autre signature {sigdiff}, inconnus de Windows {extra}");
		return 0;
	}

	static string TypeName (MetadataReader r, EntityHandle h)
	{
		if (h.Kind != HandleKind.TypeReference)
			return "(" + h.Kind + ")";
		var t = r.GetTypeReference ((TypeReferenceHandle) h);
		string n = r.GetString (t.Name), ns = r.GetString (t.Namespace);
		if (t.ResolutionScope.Kind == HandleKind.TypeReference)
			return TypeName (r, (EntityHandle) t.ResolutionScope) + "+" + n;
		return ns.Length > 0 ? ns + "." + n : n;
	}

	static string Scope (MetadataReader r, TypeReferenceHandle h)
	{
		var t = r.GetTypeReference (h);
		if (t.ResolutionScope.Kind == HandleKind.TypeReference)
			return Scope (r, (TypeReferenceHandle) t.ResolutionScope);
		return t.ResolutionScope.Kind == HandleKind.AssemblyReference
			? r.GetString (r.GetAssemblyReference ((AssemblyReferenceHandle) t.ResolutionScope).Name) : "";
	}

	static bool Ours (string asm) => asm is "System.Windows.Forms" or "System.Windows.Forms.Primitives" or "System.Drawing.Common" or "System.Drawing";

	sealed class Provider : ISignatureTypeProvider<string, object>
	{
		public string GetArrayType (string e, ArrayShape s) => e + "[]";
		public string GetByReferenceType (string e) => e + "&";
		public string GetFunctionPointerType (MethodSignature<string> s) => "fnptr";
		public string GetGenericInstantiation (string g, ImmutableArray<string> a) => g + "<" + string.Join (",", a) + ">";
		public string GetGenericMethodParameter (object c, int i) => "!!" + i;
		public string GetGenericTypeParameter (object c, int i) => "!" + i;
		public string GetModifiedType (string m, string u, bool r) => u;
		public string GetPinnedType (string e) => e;
		public string GetPointerType (string e) => e + "*";
		public string GetPrimitiveType (PrimitiveTypeCode c) => "System." + c;
		public string GetSZArrayType (string e) => e + "[]";
		public string GetTypeFromDefinition (MetadataReader r, TypeDefinitionHandle h, byte k) => r.GetString (r.GetTypeDefinition (h).Name);
		public string GetTypeFromReference (MetadataReader r, TypeReferenceHandle h, byte k) => TypeName (r, h);
		public string GetTypeFromSpecification (MetadataReader r, object c, TypeSpecificationHandle h, byte k) => r.GetTypeSpecification (h).DecodeSignature (this, c);
	}

	static string NormGeneric (string sig) => sig;

	static int Check (string[] a)
	{
		string ours = a[1], core = a[2];
		using var c = Context (new[] { ours, core });
		var ot = new Dictionary<string, Type> ();
		foreach (var f in new[] { "System.Windows.Forms", "System.Windows.Forms.Primitives", "System.Drawing.Common" }.Select (n => ours + "/" + n + ".dll").Where (File.Exists))
			foreach (var t in c.LoadFromAssemblyPath (f).GetTypes ())
				ot[t.FullName] = t;
		int bad = 0;
		foreach (var app in a.Skip (3))
		{
			using var pe = new PEReader (File.OpenRead (app));
			var r = pe.GetMetadataReader ();
			var missing = new SortedSet<string> ();
			int count = 0;
			foreach (var h in r.TypeReferences)
			{
				if (!Ours (Scope (r, h)))
					continue;
				count++;
				string n = TypeName (r, h);
				if (!ot.TryGetValue (n, out var found))
					missing.Add ("type   " + n);
				else if (found.Assembly.GetName ().Name != Scope (r, h))
					missing.Add ("type   " + n + " : attendu dans " + Scope (r, h) + ", il est dans " + found.Assembly.GetName ().Name);
			}
			var prov = new Provider ();
			foreach (var h in r.MemberReferences)
			{
				var m = r.GetMemberReference (h);
				if (m.Parent.Kind != HandleKind.TypeReference || !Ours (Scope (r, (TypeReferenceHandle) m.Parent)))
					continue;
				count++;
				string tn = TypeName (r, m.Parent), name = r.GetString (m.Name), sig;
				if (m.GetKind () == MemberReferenceKind.Method)
				{
					var s = m.DecodeMethodSignature (prov, null);
					sig = name + "(" + string.Join (",", s.ParameterTypes) + ")" + (name == ".ctor" ? "" : ":" + s.ReturnType);
				}
				else
					sig = name + ":" + m.DecodeFieldSignature (prov, null);
				if (!ot.TryGetValue (tn, out var t))
				{
					missing.Add ($"membre {tn}::{sig}  (type absent)");
					continue;
				}
				bool found = false;
				for (var b = t; b != null && !found; b = b.BaseType)
					found = Members (b).Contains (sig);
				if (!found)
					missing.Add ($"membre {tn}::{sig}");
			}
			Console.WriteLine ($"{Path.GetFileName (app)} : {count} references, {missing.Count} sans cible");
			foreach (var m in missing)
				Console.WriteLine ("  " + m);
			bad += missing.Count;
		}
		return bad == 0 ? 0 : 1;
	}

	/// <summary>Les types publics d'un assembly, et les assemblys qu'il reference.</summary>
	static int List (string file)
	{
		using var pe = new PEReader (File.OpenRead (file));
		var r = pe.GetMetadataReader ();
		foreach (var h in r.AssemblyReferences)
		{
			var a = r.GetAssemblyReference (h);
			Console.WriteLine ("ref " + r.GetString (a.Name) + " " + a.Version);
		}
		foreach (var h in r.TypeDefinitions)
		{
			var t = r.GetTypeDefinition (h);
			if (t.IsNested || (t.Attributes & TypeAttributes.VisibilityMask) != TypeAttributes.Public)
				continue;
			Console.WriteLine ("type " + r.GetString (t.Namespace) + "." + r.GetString (t.Name));
		}
		foreach (var h in r.ExportedTypes)
		{
			var t = r.GetExportedType (h);
			Console.WriteLine ("forward " + r.GetString (t.Namespace) + "." + r.GetString (t.Name));
		}
		return 0;
	}

	static int Main (string[] a)
	{
		if (a.Length > 1 && a[0] == "list")
			return List (a[1]);
		if (a.Length > 4 && a[0] == "surface")
			return Surface (a);
		if (a.Length > 3 && a[0] == "check")
			return Check (a);
		Console.WriteLine ("apidiff surface <refs Windows> <refs framework> <nos DLL> <sortie.tsv>\napidiff check <nos DLL> <refs framework> <app.dll>...");
		return 2;
	}
}
