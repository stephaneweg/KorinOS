// Font.cs -- une police d'UIKit, choisie par son nom et son style : Mono et Serif, sinon DejaVu Sans.
#nullable disable
using System.UIKit;

namespace System.Drawing;

[Flags] public enum FontStyle { Regular = 0, Bold = 1, Italic = 2, Underline = 4, Strikeout = 8 }

public sealed class Font : MarshalByRefObject, IDisposable
{
	public string Name { get; }
	public float Size { get; }
	public FontStyle Style { get; }
	public Font (string name, float size) : this (name, size, FontStyle.Regular) { }
	public Font (string name, float size, FontStyle style) { Name = name; Size = size; Style = style; }
	public void Dispose () { }

	/// <summary>uk_mt_tab, selon l'echelle du theme.</summary>
	internal static int Pixels13 => Math.Min (UI.ui_echelle (), 2) == 2 ? 12 : 13;
	internal static Font Default => new Font ("", Pixels13);
	internal long Pixels => (long) Size;

	internal long Handle
	{
		get
		{
			bool b = (Style & FontStyle.Bold) != 0, i = (Style & FontStyle.Italic) != 0;
			long f;
			if (Name != null && Name.Contains ("Mono", StringComparison.OrdinalIgnoreCase))
				f = b ? UI.UFN_MONOBOLD : UI.UFN_MONO;
			else if (Name != null && Name.Contains ("Serif", StringComparison.OrdinalIgnoreCase))
				f = b && i ? UI.UFN_SERIFBOLDITALIC : b ? UI.UFN_SERIFBOLD : i ? UI.UFN_SERIFITALIC : UI.UFN_SERIF;
			else
				f = b && i ? UI.UFN_BOLDITALIC : b ? UI.UFN_BOLD : i ? UI.UFN_ITALIC : UI.UFN_SANS;
			return UI.ui_font_get (f);
		}
	}

	internal int Width (string s) => (int) Ui.With (s ?? "", p => UI.ui_text_width (p, Pixels, Handle));
}
