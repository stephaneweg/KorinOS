// KitLoader.cs -- ouvre un kit d'ARMPI et lit sa table (docs/DOTNET.md). Les kits sont connus du noyau :
// rien n'est charge ici, armpi_kit_open demande seulement leur adresse. Pas de dlopen : « libarmpi »
// est resolu dans l'hote (PINVOKE_OVERRIDE). Les classes generees par abi2cs.py s'en servent.
#nullable disable
using System.Runtime.InteropServices;
using System.Text;

namespace System.Kits;

public static unsafe class KitLoader
{
	[DllImport ("libarmpi")] static extern nint armpi_kit_open (byte* name, uint min);
	[DllImport ("libarmpi")] static extern nint armpi_kit_fn (nint kit, uint rank);
	[DllImport ("libarmpi")] static extern void armpi_call (nint fn, long* args, long* results);

	public static nint[] Open (string name, int count)
	{
		nint kit;
		fixed (byte* n = Encoding.UTF8.GetBytes (name + "\0"))
			kit = armpi_kit_open (n, 1);
		if (kit == 0)
			throw new DllNotFoundException ("kit " + name + " absent (SD0:/lib/" + name + ".so)");
		var fn = new nint[count];
		for (int i = 0; i < count; i++)
			fn[i] = armpi_kit_fn (kit, (uint) i);
		return fn;
	}

	public static void Call (nint fn, long* args, long* results) => armpi_call (fn, args, results);

	/// <summary>Une chaine en UTF-8 terminee par NUL, a rendre par Free.</summary>
	public static long Utf8 (string s)
	{
		byte[] b = Encoding.UTF8.GetBytes (s ?? "");
		byte* p = (byte*) NativeMemory.Alloc ((nuint) b.Length + 1);
		for (int i = 0; i < b.Length; i++)
			p[i] = b[i];
		p[b.Length] = 0;
		return (long) p;
	}

	public static void Free (long p) => NativeMemory.Free ((void*) p);

	/// <summary>Une chaine rendue par un kit (elle lui appartient) -> une copie C#.</summary>
	public static string String (long p) => p == 0 ? "" : Marshal.PtrToStringUTF8 ((nint) p);
}
