// Document.cs -- DocumentKit en types C# : ce que .NET n'a pas (format d'un document, paquets OOXML, dates
// d'Excel, longueurs, couleurs). Le XML, les encodages et le CSV sont dans DK, et dans .NET.
#nullable disable
using System.Kits;

namespace System.DocumentKit;

/// <summary>Une relation d'un fichier .rels.</summary>
public readonly record struct Relation (string Id, string Type, string Target, string Mode)
{
	public bool External => Mode == "External";
}

public static unsafe class Doc
{
	delegate long Writer (long buffer, long capacity);

	// (les sorties sont comme snprintf : la longueur complete, cap - 1 octets ecrits)
	static string Text (Writer write)
	{
		byte[] buffer = new byte[512];
		while (true)
		{
			fixed (byte* p = buffer)
			{
				long n = write ((long) p, buffer.Length);
				if (n < 0)
					return null;
				if (n < buffer.Length)
					return System.Text.Encoding.UTF8.GetString (buffer, 0, (int) n);
				buffer = new byte[n + 1];
			}
		}
	}

	static long With (string s, Func<long, long> call)
	{
		long p = KitLoader.Utf8 (s);
		try { return call (p); }
		finally { KitLoader.Free (p); }
	}

	/// <summary>Le format d'un fichier d'apres son contenu (DK.DKK_*).</summary>
	public static long Kind (byte[] file)
	{
		fixed (byte* p = file)
			return DK.dk_doc_kind ((long) p, file.Length);
	}

	/// <summary>Le format d'apres un type MIME (DK.DKK_*).</summary>
	public static long KindOfMime (string mime) => With (mime, p => DK.dk_doc_kind_mime (p, -1));

	/// <summary>Le type MIME d'un format, null s'il n'en a pas.</summary>
	public static string Mime (long kind)
	{
		long p = DK.dk_doc_mime (kind);
		return p == 0 ? null : KitLoader.String (p);
	}

	/// <summary>Le chemin d'une cible relative a une partie (word/document.xml + media/a.png).</summary>
	public static string PartPath (string part, string target)
	{
		long a = KitLoader.Utf8 (part), b = KitLoader.Utf8 (target);
		try { return Text ((o, c) => DK.dk_part_path (a, b, o, c)); }
		finally { KitLoader.Free (a); KitLoader.Free (b); }
	}

	/// <summary>Le fichier .rels d'une partie (word/_rels/document.xml.rels).</summary>
	public static string RelsPath (string part)
	{
		long a = KitLoader.Utf8 (part);
		try { return Text ((o, c) => DK.dk_rels_path (a, o, c)); }
		finally { KitLoader.Free (a); }
	}

	/// <summary>Les relations d'un fichier .rels.</summary>
	public static Relation[] Relations (byte[] rels)
	{
		fixed (byte* p = rels)
		{
			long r = (long) p, n = rels.Length;
			var list = new Relation[DK.dk_rel_count (r, n)];
			for (int i = 0; i < list.Length; i++)
			{
				string Field (long field) => Text ((o, c) => DK.dk_rel_get (r, n, i, field, o, c));
				list[i] = new Relation (Field (DK.DKR_ID), Field (DK.DKR_TYPE), Field (DK.DKR_TARGET), Field (DK.DKR_MODE));
			}
			return list;
		}
	}

	/// <summary>La premiere cible interne d'un type (fin du type : slideMaster), null s'il n'y en a pas.</summary>
	public static string TargetOfType (byte[] rels, string type)
	{
		long a = KitLoader.Utf8 (type);
		try
		{
			fixed (byte* p = rels)
			{
				long r = (long) p, n = rels.Length;
				return Text ((o, c) => DK.dk_rel_by_type (r, n, a, o, c));
			}
		}
		finally { KitLoader.Free (a); }
	}

	/// <summary>Le numero de serie d'une date dans Excel.</summary>
	public static long ToExcel (DateOnly date, bool system1904 = false) =>
		DK.dk_excel_from_civil (date.Year, date.Month, date.Day, system1904 ? 1 : 0);

	/// <summary>La date d'un numero de serie d'Excel.</summary>
	public static DateOnly FromExcel (long serial, bool system1904 = false)
	{
		int y = 0, m = 0, d = 0;
		DK.dk_civil_from_excel (serial, system1904 ? 1 : 0, (long) &y, (long) &m, (long) &d);
		return new DateOnly (y, m, d);
	}

	/// <summary>Une longueur d'une unite dans une autre (DK.DKU_*), arrondie.</summary>
	public static long ConvertLength (long value, long unit, long to) => DK.dk_len_conv (value, unit, to);

	/// <summary>Une couleur (#RRGGBB, RRGGBB, AARRGGBB) en 0xRRGGBB, fallback si le texte n'en est pas une.</summary>
	public static long ParseColor (string text, long fallback = 0) => With (text, p => DK.dk_color_parse (p, fallback));
}
