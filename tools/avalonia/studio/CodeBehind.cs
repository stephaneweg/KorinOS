// CodeBehind.cs -- le lien entre un evenement du .axaml et sa methode dans le .axaml.cs : ajout au texte, sans reformater.
using System;
using System.Text.RegularExpressions;

namespace AvStudio;

public static class CodeBehind
{
	/// <summary>Le fichier de code d'un .axaml, a cote de lui.</summary>
	public static string CodeFile (string xamlPath) => xamlPath + ".cs";

	/// <summary>La ligne (1..) ou la methode est declaree, 0 si elle n'y est pas.</summary>
	public static int FindMethod (string code, string method)
	{
		var m = Regex.Match (code, @"^[ \t]*(?:[\w<>\[\]?,.]+[ \t]+)+" + Regex.Escape (method) + @"[ \t]*\(", RegexOptions.Multiline);
		return m.Success ? LineOf (code, m.Index) : 0;
	}

	static int LineOf (string code, int offset)
	{
		int line = 1;
		for (int i = 0; i < offset; i++)
			if (code[i] == '\n')
				line++;
		return line;
	}

	/// <summary>Ajoute la methode a la fin de la classe, si elle n'existe pas -> le texte et sa ligne.</summary>
	public static (string Text, int Line) AddHandler (string code, string className, string method, string parameters)
	{
		int found = FindMethod (code, method);
		if (found != 0)
			return (code, found);
		var m = Regex.Match (code, @"\bclass[ \t]+" + Regex.Escape (className) + @"\b");
		if (!m.Success)
			throw new ArgumentException ("classe introuvable : " + className);
		int close = ClassEnd (code, code.IndexOf ('{', m.Index));
		string nl = code.Contains ("\r\n") ? "\r\n" : "\n";
		int line = close;
		while (line > 0 && (code[line - 1] == ' ' || code[line - 1] == '\t'))
			line--;
		string outer = code.Substring (line, close - line);
		string tab = code.Contains ("\n\t") ? "\t" : "    ";
		string indent = outer + tab;
		string space = code.Contains (") \n") || Regex.IsMatch (code, @"\w \(") ? " " : "";
		string body = (code.Substring (0, line).TrimEnd ().EndsWith ("{") ? "" : nl) + indent + "void " + method + space + "(" + parameters + ")" + nl
			+ indent + "{" + nl + indent + "}" + nl;
		string text = code.Insert (line, body);
		return (text, FindMethod (text, method));
	}

	/// <summary>L'accolade qui ferme celle de open, chaines et commentaires ignores.</summary>
	static int ClassEnd (string code, int open)
	{
		int depth = 0;
		for (int i = open; i < code.Length; i++)
		{
			char c = code[i];
			if (c == '/' && i + 1 < code.Length && code[i + 1] == '/')
				i = code.IndexOf ('\n', i);
			else if (c == '/' && i + 1 < code.Length && code[i + 1] == '*')
				i = code.IndexOf ("*/", i, StringComparison.Ordinal) + 1;
			else if (c == '"' || c == '\'')
			{
				for (i++; code[i] != c; i++)
					if (code[i] == '\\')
						i++;
			}
			else if (c == '{')
				depth++;
			else if (c == '}' && --depth == 0)
				return i;
			if (i < 0)
				break;
		}
		throw new ArgumentException ("accolades non equilibrees");
	}
}
