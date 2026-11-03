// XamlText.cs -- le texte d'un .axaml reste la reference de l'editeur : chaque action visuelle y fait une modification
// minimale, sans reecrire le reste (mise en forme, commentaires, ordre des attributs). Sans Avalonia : essaye par --selftest.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace AvStudio;

/// <summary>Une balise ouvrante dans le texte.</summary>
public sealed class XamlTag
{
	public int Start, NameEnd, End;			// '<', fin du nom, apres '>'
	public bool SelfClosed;
	public string Name = "";
	public readonly List<XamlAttribute> Attributes = new ();
}

public sealed record XamlAttribute (string Name, int Start, int End, int ValueStart, int ValueEnd);

public static class XamlText
{
	public static XDocument? Parse (string text)
	{
		try { return XDocument.Parse (text, LoadOptions.SetLineInfo | LoadOptions.PreserveWhitespace); }
		catch (XmlException) { return null; }
	}

	/// <summary>Les elements dans l'ordre du document : leur rang les designe d'une edition a l'autre.</summary>
	public static List<XElement> Elements (XDocument doc) => doc.Descendants ().ToList ();

	static int OffsetOf (string text, IXmlLineInfo info)
	{
		int at = 0;
		for (int line = 1; line < info.LineNumber; line++)
		{
			at = text.IndexOf ('\n', at);
			if (at < 0)
				throw new ArgumentException ("ligne hors du texte");
			at++;
		}
		return at + info.LinePosition - 1;
	}

	/// <summary>La balise ouvrante de l'element.</summary>
	public static XamlTag TagOf (string text, XElement e)
	{
		int at = OffsetOf (text, e);
		while (at > 0 && text[at] != '<')
			at--;
		return ReadTag (text, at);
	}

	static XamlTag ReadTag (string text, int at)
	{
		var tag = new XamlTag { Start = at };
		int i = at + 1;
		while (!char.IsWhiteSpace (text[i]) && text[i] != '>' && text[i] != '/')
			i++;
		tag.NameEnd = i;
		tag.Name = text.Substring (at + 1, i - at - 1);
		for (;;)
		{
			while (char.IsWhiteSpace (text[i]))
				i++;
			if (text[i] == '>')
			{
				tag.End = i + 1;
				return tag;
			}
			if (text[i] == '/')
			{
				tag.SelfClosed = true;
				tag.End = i + 2;
				return tag;
			}
			int start = i;
			while (text[i] != '=' && !char.IsWhiteSpace (text[i]))
				i++;
			string name = text.Substring (start, i - start);
			while (text[i] != '"' && text[i] != '\'')
				i++;
			char quote = text[i];
			int value = i + 1;
			i = text.IndexOf (quote, value);
			tag.Attributes.Add (new XamlAttribute (name, start, i + 1, value, i));
			i++;
		}
	}

	/// <summary>L'offset de la balise fermante d'un element non vide.</summary>
	static int CloseOf (string text, XamlTag open)
	{
		int depth = 1, i = open.End;
		for (;;)
		{
			i = text.IndexOf ('<', i);
			if (i < 0)
				throw new ArgumentException ("balise non fermee");
			if (string.CompareOrdinal (text, i, "<!--", 0, 4) == 0)
				i = text.IndexOf ("-->", i, StringComparison.Ordinal) + 3;
			else if (string.CompareOrdinal (text, i, "<![CDATA[", 0, 9) == 0)
				i = text.IndexOf ("]]>", i, StringComparison.Ordinal) + 3;
			else if (text[i + 1] == '/')
			{
				if (--depth == 0)
					return i;
				i += 2;
			}
			else
			{
				var t = ReadTag (text, i);
				if (!t.SelfClosed)
					depth++;
				i = t.End;
			}
		}
	}

	static string Escape (string value) => value.Replace ("&", "&amp;").Replace ("\"", "&quot;").Replace ("<", "&lt;");

	/// <summary>Pose, change ou retire (value null) un attribut, le reste du texte ne bouge pas.</summary>
	public static string SetAttribute (string text, XElement e, string name, string? value)
	{
		var tag = TagOf (text, e);
		var old = tag.Attributes.FirstOrDefault (a => a.Name == name);
		if (old != null)
		{
			if (value != null)
				return text.Substring (0, old.ValueStart) + Escape (value) + text.Substring (old.ValueEnd);
			int from = old.Start;
			while (from > 0 && char.IsWhiteSpace (text[from - 1]))
				from--;
			return text.Remove (from, old.End - from);
		}
		if (value == null)
			return text;
		// (sur la ligne de la balise, apres le dernier attribut)
		int at = tag.Attributes.Count > 0 ? tag.Attributes[^1].End : tag.NameEnd;
		return text.Insert (at, " " + name + "=\"" + Escape (value) + "\"");
	}

	static string IndentAt (string text, int at)
	{
		int line = text.LastIndexOf ('\n', Math.Max (at - 1, 0)) + 1, i = line;
		while (i < text.Length && (text[i] == ' ' || text[i] == '\t'))
			i++;
		return text.Substring (line, i - line);
	}

	/// <summary>Ajoute un enfant a la fin de l'element ; une balise vide s'ouvre.</summary>
	public static string AddChild (string text, XElement parent, string child)
	{
		var tag = TagOf (text, parent);
		string indent = IndentAt (text, tag.Start), inner = indent + (indent.Contains ('\t') ? "\t" : "  ");
		string nl = text.Contains ("\r\n") ? "\r\n" : "\n";
		if (tag.SelfClosed)
		{
			string open = text.Substring (tag.Start, tag.End - tag.Start - 2).TrimEnd ();
			return text.Substring (0, tag.Start) + open + ">" + nl + inner + child + nl + indent + "</" + tag.Name + ">" + text.Substring (tag.End);
		}
		int close = CloseOf (text, tag), line = close;
		while (line > 0 && (text[line - 1] == ' ' || text[line - 1] == '\t'))
			line--;
		if (line > 0 && text[line - 1] == '\n')
			return text.Insert (line, inner + child + nl);
		return text.Insert (close, nl + inner + child + nl + indent);
	}

	/// <summary>Retire l'element, avec sa ligne si elle ne contient que lui.</summary>
	public static string Remove (string text, XElement e)
	{
		var tag = TagOf (text, e);
		int end = tag.SelfClosed ? tag.End : CloseOf (text, tag) + tag.Name.Length + 3;
		int start = tag.Start;
		while (start > 0 && (text[start - 1] == ' ' || text[start - 1] == '\t'))
			start--;
		if (start == 0 || text[start - 1] == '\n')
		{
			if (text.Substring (end).StartsWith ("\r\n"))
				end += 2;
			else if (end < text.Length && text[end] == '\n')
				end++;
		}
		else
			start = tag.Start;
		return text.Remove (start, end - start);
	}

	/// <summary>Echange l'element avec son voisin : delta -1 monte, +1 descend.</summary>
	public static string Move (string text, XElement e, int delta)
	{
		var sibling = (delta < 0 ? e.ElementsBeforeSelf ().LastOrDefault () : e.ElementsAfterSelf ().FirstOrDefault ());
		if (sibling == null)
			return text;
		var (first, second) = delta < 0 ? (sibling, e) : (e, sibling);
		var (a, b) = (SpanOf (text, first), SpanOf (text, second));
		return text.Substring (0, a.Start) + text.Substring (b.Start, b.End - b.Start) + text.Substring (a.End, b.Start - a.End)
			+ text.Substring (a.Start, a.End - a.Start) + text.Substring (b.End);
	}

	static (int Start, int End) SpanOf (string text, XElement e)
	{
		var tag = TagOf (text, e);
		return (tag.Start, tag.SelfClosed ? tag.End : CloseOf (text, tag) + tag.Name.Length + 3);
	}

	/// <summary>Pose xmlns:prefix sur la racine s'il n'y est pas.</summary>
	public static string EnsureNamespace (string text, XDocument doc, string prefix, string ns)
	{
		var root = doc.Root!;
		var existing = root.Attributes ().FirstOrDefault (a => a.IsNamespaceDeclaration && a.Value == ns);
		return existing != null ? text : SetAttribute (text, root, "xmlns:" + prefix, ns);
	}

	/// <summary>Le prefixe deja declare pour cet espace de noms.</summary>
	public static string? PrefixOf (XDocument doc, string ns) => doc.Root!.GetPrefixOfNamespace (ns);

	public static XElement? ElementAtLine (XDocument doc, int line)
	{
		XElement? found = null;
		foreach (var e in doc.Descendants ())
			if (((IXmlLineInfo) e).LineNumber <= line)
				found = e;
			else
				break;
		return found;
	}

	/// <summary>Essais sans fenetre : avstudio --selftest.</summary>
	public static int SelfTest ()
	{
		const string src = "<!-- garde -->\n<Grid  Width=\"10\"\n      Height='20'>\n  <TextBlock Text=\"a&amp;b\"/>\n  <StackPanel>\n    <Button />\n  </StackPanel>\n</Grid>\n";
		int fail = 0;
		void Check (string name, string got, string want)
		{
			if (got == want)
				return;
			fail++;
			Console.WriteLine ("ECHEC " + name + "\n--- obtenu\n" + got + "\n--- attendu\n" + want);
		}
		XDocument D (string s) => Parse (s)!;
		XElement E (string s, int i) => Elements (D (s))[i];
		Check ("changer", SetAttribute (src, E (src, 0), "Width", "30"), src.Replace ("\"10\"", "\"30\""));
		Check ("apostrophe", SetAttribute (src, E (src, 0), "Height", "1"), src.Replace ("'20'", "'1'"));
		Check ("ajouter", SetAttribute (src, E (src, 1), "Margin", "4"), src.Replace ("a&amp;b\"/>", "a&amp;b\" Margin=\"4\"/>"));
		Check ("ajouter sans attribut", SetAttribute (src, E (src, 2), "Spacing", "2"), src.Replace ("<StackPanel>", "<StackPanel Spacing=\"2\">"));
		Check ("retirer", SetAttribute (src, E (src, 0), "Width", null), src.Replace ("  Width=\"10\"", ""));
		Check ("echapper", SetAttribute (src, E (src, 1), "Text", "x<\"y"), src.Replace ("a&amp;b", "x&lt;&quot;y"));
		Check ("enfant, fin", AddChild (src, E (src, 2), "<Label/>"), src.Replace ("<Button />\n", "<Button />\n    <Label/>\n"));
		Check ("enfant, balise vide", AddChild (src, E (src, 3), "<Label/>"), src.Replace ("<Button />", "<Button>\n      <Label/>\n    </Button>"));
		Check ("enlever", Remove (src, E (src, 1)), src.Replace ("  <TextBlock Text=\"a&amp;b\"/>\n", ""));
		Check ("enlever, conteneur", Remove (src, E (src, 2)), src.Replace ("  <StackPanel>\n    <Button />\n  </StackPanel>\n", ""));
		Check ("descendre", Move (src, E (src, 1), 1), "<!-- garde -->\n<Grid  Width=\"10\"\n      Height='20'>\n  <StackPanel>\n    <Button />\n  </StackPanel>\n  <TextBlock Text=\"a&amp;b\"/>\n</Grid>\n");
		Check ("monter", Move (src, E (src, 2), -1), Move (src, E (src, 1), 1));
		string nested = "<A><B/><!-- </A> --><C></C></A>";
		Check ("commentaire ignore", AddChild (nested, E (nested, 0), "<D/>"), "<A><B/><!-- </A> --><C></C>\n  <D/>\n</A>");
		Check ("ligne", ElementAtLine (D (src), 6)!.Name.LocalName, "Button");
		const string code = "using System;\n\nnamespace N;\n\npublic partial class Foo : Window\n{\n\tstring s = \"}\";\n\n\tvoid A ()\n\t{\n\t}\n}\n";
		var (added, line) = CodeBehind.AddHandler (code, "Foo", "OnClick", "object? sender, EventArgs e");
		Check ("code-behind", added, code.Replace ("\tvoid A ()\n\t{\n\t}\n}", "\tvoid A ()\n\t{\n\t}\n\n\tvoid OnClick (object? sender, EventArgs e)\n\t{\n\t}\n}"));
		Check ("ligne de la methode", line.ToString (), "13");
		Check ("methode trouvee", CodeBehind.FindMethod (added, "OnClick").ToString (), "13");
		Check ("methode deja la", CodeBehind.AddHandler (added, "Foo", "OnClick", "x").Text, added);
		Console.WriteLine (fail == 0 ? "selftest : ok" : "selftest : " + fail + " echec(s)");
		return fail == 0 ? 0 : 1;
	}
}
