// Ui.cs -- ce que le .abi d'UIKit ne dit pas : les variables de l'interface (ui_data des programmes en
// assembleur, struct ui_vars en C) et l'evenement courant.
#nullable disable
using System.Kits;
using System.Runtime.InteropServices;

namespace System.UIKit;

public static unsafe class Ui
{
	const int VarsSize = 64 + 3 * (int) UI.UI_FONT_SIZE;
	const int EvOffset = 32;
	static byte* s_vars;

	/// <summary>Ouvre UIKit et charge ses polices, une seule fois.</summary>
	public static void Init ()
	{
		if (s_vars != null)
			return;
		s_vars = (byte*) NativeMemory.AllocZeroed ((nuint) VarsSize);
		if (UI.ui_init ((long) s_vars) == 0)
			throw new InvalidOperationException ("UIKit : polices absentes (SD0:/fonts)");
	}

	public static bool Ready => s_vars != null;
	public static long Width => ((long*) s_vars)[2];
	public static long Height => ((long*) s_vars)[3];

	/// <summary>Un champ 32 bits du dernier evenement (EV_*).</summary>
	public static int Event (long offset) => *(int*) (s_vars + EvOffset + offset);

	/// <summary>Une couleur du theme du bureau (TJ_*).</summary>
	public static uint Token (long offset) => *(uint*) (UI.ui_jetons () + offset);

	/// <summary>Appelle un kit avec des chaines, rendues au retour.</summary>
	public static long With (string s, Func<long, long> call)
	{
		long p = KitLoader.Utf8 (s);
		try { return call (p); }
		finally { KitLoader.Free (p); }
	}
}
