// Office.cs -- DocumentKit en objets : le contenu d'un document de bureau, a lire et a modifier sans
// ouvrir d'application (docs/DOTNET.md). Tout ce qui n'est pas touche reste tel quel dans le fichier : images,
// styles, mise en page.
//
//     var doc = Document.Open ("/home/rapport.docx");
//     foreach (var p in doc.Paragraphs) Console.WriteLine (p.Text);
//
//     var book = Spreadsheet.Open ("/home/compta.xlsx");
//     book.Sheets[0].Cells["B2"].Value = 42;
//     book.Save ();
#nullable disable
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace System.DocumentKit;

/// <summary>Les fichiers d'un ZIP de bureau, en memoire, et leurs XML deja lus.</summary>
sealed class Package
{
	readonly List<string> _names = new ();
	readonly Dictionary<string, byte[]> _files = new ();
	readonly Dictionary<string, XDocument> _xml = new ();

	internal Package () { }

	internal Package (string path)
	{
		using var zip = ZipFile.OpenRead (path);
		foreach (var e in zip.Entries)
		{
			using var s = e.Open ();
			var m = new MemoryStream ();
			s.CopyTo (m);
			Set (e.FullName, m.ToArray ());
		}
	}

	internal bool Has (string name) => _files.ContainsKey (name) || _xml.ContainsKey (name);
	internal byte[] Bytes (string name) => _files[name];

	internal void Set (string name, byte[] data)
	{
		if (!Has (name))
			_names.Add (name);
		_files[name] = data;
	}

	internal void Set (string name, string text) => Set (name, new UTF8Encoding (false).GetBytes (text));

	internal void Remove (string name)
	{
		_names.Remove (name);
		_files.Remove (name);
		_xml.Remove (name);
	}

	/// <summary>Le XML d'un fichier, gardé pour Save.</summary>
	internal XDocument Xml (string name)
	{
		if (!_xml.TryGetValue (name, out var x))
		{
			using var m = new MemoryStream (_files[name]);
			_xml[name] = x = XDocument.Load (m, LoadOptions.PreserveWhitespace);
		}
		return x;
	}

	internal void Save (string path)
	{
		foreach (var (name, x) in _xml)
		{
			var m = new MemoryStream ();
			var settings = new XmlWriterSettings { Encoding = new UTF8Encoding (false) };
			using (var w = XmlWriter.Create (m, settings))
				x.Save (w);
			_files[name] = m.ToArray ();
		}
		string temporary = path + ".tmp";
		using (var file = File.Create (temporary))
		using (var zip = new ZipArchive (file, ZipArchiveMode.Create))
		{
			// (ODF : mimetype d'abord, sans compression ; dk_doc_kind s'y fie aussi)
			foreach (var name in _names.OrderBy (n => n == "mimetype" ? 0 : 1))
			{
				var e = zip.CreateEntry (name, name == "mimetype" ? CompressionLevel.NoCompression : CompressionLevel.Optimal);
				using var s = e.Open ();
				s.Write (_files[name]);
			}
		}
		File.Delete (path);
		File.Move (temporary, path);
	}
}

/// <summary>Ce qui ne depend que du format : espaces de noms des trois familles de paragraphes.</summary>
static class Ns
{
	internal static readonly XNamespace W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
	internal static readonly XNamespace A = "http://schemas.openxmlformats.org/drawingml/2006/main";
	internal static readonly XNamespace P = "http://schemas.openxmlformats.org/presentationml/2006/main";
	internal static readonly XNamespace S = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
	internal static readonly XNamespace R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
	internal static readonly XNamespace Office = "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
	internal static readonly XNamespace Text = "urn:oasis:names:tc:opendocument:xmlns:text:1.0";
	internal static readonly XNamespace Table = "urn:oasis:names:tc:opendocument:xmlns:table:1.0";
	internal static readonly XNamespace Draw = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0";
	internal static readonly XNamespace Presentation = "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
	internal static readonly XNamespace Types = "http://schemas.openxmlformats.org/package/2006/content-types";
	internal static readonly XNamespace Rels = "http://schemas.openxmlformats.org/package/2006/relationships";

	/// <summary>Le format d'un fichier ; un ZIP dont le mimetype est compresse : d'apres ce mimetype.</summary>
	internal static long Kind (string path)
	{
		long kind = Doc.Kind (File.ReadAllBytes (path));
		if (kind != DK.DKK_ZIP)
			return kind;
		using var zip = ZipFile.OpenRead (path);
		var mime = zip.GetEntry ("mimetype");
		if (mime == null)
			return kind;
		using var r = new StreamReader (mime.Open ());
		return Doc.KindOfMime (r.ReadToEnd ().Trim ());
	}

	internal const string XmlHead = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
	internal const string Manifest = XmlHead + "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
		+ "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"{0}\"/>"
		+ "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>";
	internal const string RootRels = XmlHead + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		+ "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"{0}\"/></Relationships>";

	/// <summary>La partie principale d'un paquet OOXML, d'apres _rels/.rels.</summary>
	internal static string MainPart (Package p, string fallback)
	{
		string target = p.Has ("_rels/.rels") ? Doc.TargetOfType (p.Bytes ("_rels/.rels"), "officeDocument") : null;
		return string.IsNullOrEmpty (target) ? fallback : target.TrimStart ('/');
	}

	/// <summary>Le fichier vise par une relation d'une partie, sans « / » de tete.</summary>
	internal static string Target (Package p, string part, string id)
	{
		foreach (var r in Doc.Relations (p.Bytes (Doc.RelsPath (part))))
			if (r.Id == id)
				return r.Target.StartsWith ('/') ? r.Target.TrimStart ('/') : Doc.PartPath (part, r.Target);
		return null;
	}

	internal static void SetOdfRoot (Package p, string mime, string content)
	{
		p.Set ("mimetype", mime);
		p.Set ("META-INF/manifest.xml", string.Format (Manifest, mime));
		p.Set ("content.xml", XmlHead + content);
	}
}

enum Dialect { Word, Drawing, Odf }

/// <summary>Un paragraphe d'un document ou d'une diapositive. Changer Text remplace ses morceaux par un seul,
/// avec la mise en forme du premier.</summary>
public sealed class Paragraph
{
	readonly XElement _e;
	readonly Dialect _dialect;
	internal XElement Element => _e;

	internal Paragraph (XElement e, Dialect dialect) { _e = e; _dialect = dialect; }

	/// <summary>Le nom du style du paragraphe, null s'il n'en a pas.</summary>
	public string Style
	{
		get => _dialect switch
		{
			Dialect.Word => (string) _e.Element (Ns.W + "pPr")?.Element (Ns.W + "pStyle")?.Attribute (Ns.W + "val"),
			Dialect.Odf => (string) _e.Attribute (Ns.Text + "style-name"),
			_ => null,
		};
		set
		{
			if (_dialect == Dialect.Odf)
				_e.SetAttributeValue (Ns.Text + "style-name", value);
			else if (_dialect == Dialect.Word)
			{
				var pr = _e.Element (Ns.W + "pPr");
				if (pr == null)
					_e.AddFirst (pr = new XElement (Ns.W + "pPr"));
				pr.Element (Ns.W + "pStyle")?.Remove ();
				if (value != null)
					pr.AddFirst (new XElement (Ns.W + "pStyle", new XAttribute (Ns.W + "val", value)));
			}
		}
	}

	/// <summary>Le texte : « \t » pour une tabulation, « \n » pour un saut de ligne.</summary>
	public string Text
	{
		get
		{
			var b = new StringBuilder ();
			if (_dialect == Dialect.Odf)
				Odf (_e, b);
			else
				foreach (var e in _e.Descendants ())
					Word (e, b);
			return b.ToString ();
		}
		set
		{
			if (_dialect == Dialect.Odf)
			{
				_e.Nodes ().Remove ();
				_e.Add (value);
				return;
			}
			XNamespace ns = _dialect == Dialect.Word ? Ns.W : Ns.A;
			XName pr = ns + "rPr";
			var first = _e.Elements (ns + "r").FirstOrDefault ()?.Element (pr);
			_e.Elements (ns + "r").Remove ();
			var run = new XElement (ns + "r");
			if (first != null)
				run.Add (new XElement (first));
			var t = new XElement (ns + "t", value);
			if (_dialect == Dialect.Word)
				t.SetAttributeValue (XNamespace.Xml + "space", "preserve");
			run.Add (t);
			var end = _e.Element (ns + "endParaRPr");
			if (end != null)
				end.AddBeforeSelf (run);
			else
				_e.Add (run);
		}
	}

	void Word (XElement e, StringBuilder b)
	{
		XNamespace ns = _dialect == Dialect.Word ? Ns.W : Ns.A;
		if (e.Name == ns + "t")
			b.Append (e.Value);
		else if (e.Name == ns + "tab" && e.Parent?.Name == ns + "r")
			b.Append ('\t');
		else if (e.Name == ns + "br")
			b.Append ('\n');
	}

	static void Odf (XElement e, StringBuilder b)
	{
		foreach (var n in e.Nodes ())
		{
			if (n is XText t)
				b.Append (t.Value);
			else if (n is XElement c)
			{
				if (c.Name == Ns.Text + "tab")
					b.Append ('\t');
				else if (c.Name == Ns.Text + "line-break")
					b.Append ('\n');
				else if (c.Name == Ns.Text + "s")
					b.Append (' ', (int?) c.Attribute (Ns.Text + "c") ?? 1);
				else
					Odf (c, b);
			}
		}
	}
}

/// <summary>Un texte : .docx ou .odt.</summary>
public sealed class Document
{
	readonly Package _package;
	readonly long _kind;
	readonly XElement _body;
	readonly List<Paragraph> _paragraphs;
	string _path;

	/// <summary>Le chemin du fichier, null tant que le document n'a pas ete enregistre.</summary>
	public string Path => _path;
	public IReadOnlyList<Paragraph> Paragraphs => _paragraphs;
	/// <summary>Tout le texte, un paragraphe par ligne.</summary>
	public string Text => string.Join ("\n", _paragraphs.Select (p => p.Text));

	Document (Package package, long kind, string path)
	{
		_package = package;
		_kind = kind;
		_path = path;
		if (kind == DK.DKK_DOCX)
		{
			_body = package.Xml (Ns.MainPart (package, "word/document.xml")).Root.Element (Ns.W + "body");
			_paragraphs = _body.Descendants (Ns.W + "p").Select (e => new Paragraph (e, Dialect.Word)).ToList ();
		}
		else
		{
			_body = package.Xml ("content.xml").Root.Element (Ns.Office + "body").Element (Ns.Office + "text");
			_paragraphs = _body.Descendants ().Where (e => e.Name == Ns.Text + "p" || e.Name == Ns.Text + "h").Select (e => new Paragraph (e, Dialect.Odf)).ToList ();
		}
	}

	public static Document Open (string path)
	{
		long kind = Ns.Kind (path);
		if (kind != DK.DKK_DOCX && kind != DK.DKK_ODT)
			throw new NotSupportedException ("Document : ni .docx ni .odt : " + path);
		return new Document (new Package (path), kind, path);
	}

	/// <summary>Un document vide, DK.DKK_DOCX ou DK.DKK_ODT.</summary>
	public static Document Create (long kind = DK.DKK_DOCX)
	{
		var p = new Package ();
		if (kind == DK.DKK_DOCX)
		{
			p.Set ("[Content_Types].xml", Ns.XmlHead + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
				+ "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
				+ "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>");
			p.Set ("_rels/.rels", string.Format (Ns.RootRels, "word/document.xml"));
			p.Set ("word/document.xml", Ns.XmlHead + "<w:document xmlns:w=\"" + Ns.W + "\"><w:body><w:sectPr/></w:body></w:document>");
		}
		else if (kind == DK.DKK_ODT)
			Ns.SetOdfRoot (p, "application/vnd.oasis.opendocument.text", "<office:document-content xmlns:office=\"" + Ns.Office + "\" xmlns:text=\"" + Ns.Text
				+ "\" office:version=\"1.3\"><office:body><office:text/></office:body></office:document-content>");
		else
			throw new NotSupportedException ("Document : ni DKK_DOCX ni DKK_ODT");
		return new Document (p, kind, null);
	}

	/// <summary>Un paragraphe de plus, a la fin.</summary>
	public Paragraph Add (string text, string style = null)
	{
		Paragraph p;
		if (_kind == DK.DKK_DOCX)
		{
			var e = new XElement (Ns.W + "p");
			var section = _body.Element (Ns.W + "sectPr");
			if (section != null)
				section.AddBeforeSelf (e);
			else
				_body.Add (e);
			p = new Paragraph (e, Dialect.Word);
		}
		else
		{
			var e = new XElement (Ns.Text + "p");
			_body.Add (e);
			p = new Paragraph (e, Dialect.Odf);
		}
		p.Text = text;
		if (style != null)
			p.Style = style;
		_paragraphs.Add (p);
		return p;
	}

	public void Remove (Paragraph p)
	{
		if (_paragraphs.Remove (p))
			p.Element.Remove ();
	}

	/// <summary>Enregistre ; path : un autre fichier, qui devient le sien. Le format ne change pas.</summary>
	public void Save (string path = null)
	{
		_path = path ?? _path ?? throw new InvalidOperationException ("Document : pas de chemin");
		_package.Save (_path);
	}
}

/// <summary>Une cellule. Value : null, string, double ou bool ; lire Formula, ecrire Value l'enleve.</summary>
public sealed class Cell
{
	readonly Sheet _sheet;
	internal object Raw;
	internal string RawFormula;
	internal XElement Xlsx;			// son <c>, s'il existe
	internal string OdfStyle;
	public int Row { get; }			// (0 : la premiere)
	public int Column { get; }
	public string Address => Spreadsheet.Name (Column) + (Row + 1).ToString (CultureInfo.InvariantCulture);
	public string Formula => RawFormula;

	internal Cell (Sheet sheet, int row, int column) { _sheet = sheet; Row = row; Column = column; }

	public object Value
	{
		get => Raw;
		set
		{
			Raw = value switch
			{
				null or string or bool or double => value,
				IConvertible c => c.ToDouble (CultureInfo.InvariantCulture),
				_ => throw new ArgumentException ("Cell : valeur ni texte, ni nombre, ni booleen"),
			};
			if (RawFormula != null)
				_sheet.Book.FormulaGone ();
			RawFormula = null;
			_sheet.Touch (this);
		}
	}

	public string Text => Raw switch
	{
		null => "",
		double d => d.ToString (CultureInfo.InvariantCulture),
		bool b => b ? "TRUE" : "FALSE",
		_ => (string) Raw,
	};
}

/// <summary>Les cellules d'une feuille, par adresse (« B2 ») ou par rang (0 : la premiere).</summary>
public sealed class CellCollection
{
	readonly Sheet _sheet;
	internal readonly Dictionary<(int Row, int Column), Cell> Map = new ();
	internal CellCollection (Sheet sheet) { _sheet = sheet; }

	public Cell this[int row, int column]
	{
		get
		{
			if (!Map.TryGetValue ((row, column), out var c))
				Map[(row, column)] = c = new Cell (_sheet, row, column);
			return c;
		}
	}

	public Cell this[string address]
	{
		get
		{
			var (row, column) = Spreadsheet.Parse (address);
			return this[row, column];
		}
	}
}

public sealed class Sheet
{
	internal readonly Spreadsheet Book;
	internal readonly XElement Xml;		// <sheetData> ou <table:table>
	internal readonly List<Cell> Changed = new ();
	string _name;
	internal Action<string> Rename;
	public CellCollection Cells { get; }
	public string Name { get => _name; set { _name = value; Rename?.Invoke (value); } }
	/// <summary>Les cellules qui ont une valeur, ligne par ligne.</summary>
	public IEnumerable<Cell> Used => Cells.Map.Values.Where (c => c.Raw != null).OrderBy (c => c.Row).ThenBy (c => c.Column);
	public int RowCount => Used.Select (c => c.Row + 1).DefaultIfEmpty ().Max ();
	public int ColumnCount => Used.Select (c => c.Column + 1).DefaultIfEmpty ().Max ();

	internal Sheet (Spreadsheet book, string name, XElement xml) { Book = book; _name = name; Xml = xml; Cells = new (this); }
	internal void Touch (Cell c) { if (!Changed.Contains (c)) Changed.Add (c); }
}

/// <summary>Un classeur : .xlsx, .ods ou .csv.</summary>
public sealed class Spreadsheet
{
	const long Csv = -1;
	readonly Package _package;
	readonly long _kind;
	readonly List<Sheet> _sheets = new ();
	string _path, _workbook;
	char _separator = ',';
	bool _noCalcChain;

	public string Path => _path;
	public IReadOnlyList<Sheet> Sheets => _sheets;
	public Sheet this[string name] => _sheets.FirstOrDefault (s => s.Name == name);

	/// <summary>Le nom d'une colonne (0 : A, 26 : AA).</summary>
	public static string Name (int column)
	{
		string s = "";
		for (column++; column > 0; column = (column - 1) / 26)
			s = (char) ('A' + (column - 1) % 26) + s;
		return s;
	}

	internal static (int Row, int Column) Parse (string address)
	{
		int i = 0, column = 0;
		for (; i < address.Length && char.IsAsciiLetter (address[i]); i++)
			column = column * 26 + char.ToUpperInvariant (address[i]) - 'A' + 1;
		if (i == 0 || i == address.Length || !int.TryParse (address.AsSpan (i), out int row) || row < 1)
			throw new ArgumentException ("adresse de cellule : " + address);
		return (row - 1, column - 1);
	}

	Spreadsheet (Package package, long kind, string path) { _package = package; _kind = kind; _path = path; }

	public static Spreadsheet Open (string path)
	{
		long kind = Ns.Kind (path);
		if (kind == DK.DKK_XLSX)
		{
			var s = new Spreadsheet (new Package (path), kind, path);
			s.ReadXlsx ();
			return s;
		}
		if (kind == DK.DKK_ODS)
		{
			var s = new Spreadsheet (new Package (path), kind, path);
			s.ReadOds ();
			return s;
		}
		if (System.IO.Path.GetExtension (path).Equals (".csv", StringComparison.OrdinalIgnoreCase))
		{
			var s = new Spreadsheet (null, Csv, path);
			s.ReadCsv (File.ReadAllBytes (path));
			return s;
		}
		throw new NotSupportedException ("Spreadsheet : ni .xlsx, ni .ods, ni .csv : " + path);
	}

	/// <summary>Un classeur d'une feuille, DK.DKK_XLSX ou DK.DKK_ODS.</summary>
	public static Spreadsheet Create (long kind = DK.DKK_XLSX)
	{
		var p = new Package ();
		var s = new Spreadsheet (p, kind, null);
		if (kind == DK.DKK_XLSX)
		{
			p.Set ("[Content_Types].xml", Ns.XmlHead + "<Types xmlns=\"" + Ns.Types + "\">"
				+ "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
				+ "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/></Types>");
			p.Set ("_rels/.rels", string.Format (Ns.RootRels, "xl/workbook.xml"));
			p.Set ("xl/workbook.xml", Ns.XmlHead + "<workbook xmlns=\"" + Ns.S + "\" xmlns:r=\"" + Ns.R + "\"><sheets/></workbook>");
			p.Set ("xl/_rels/workbook.xml.rels", Ns.XmlHead + "<Relationships xmlns=\"" + Ns.Rels + "\"/>");
			s.ReadXlsx ();
		}
		else if (kind == DK.DKK_ODS)
		{
			Ns.SetOdfRoot (p, "application/vnd.oasis.opendocument.spreadsheet", "<office:document-content xmlns:office=\"" + Ns.Office + "\" xmlns:table=\"" + Ns.Table + "\" xmlns:text=\"" + Ns.Text
				+ "\" office:version=\"1.3\"><office:body><office:spreadsheet/></office:body></office:document-content>");
			s.ReadOds ();
		}
		else
			throw new NotSupportedException ("Spreadsheet : ni DKK_XLSX ni DKK_ODS");
		s.AddSheet ("Feuille1");
		return s;
	}

	/// <summary>Une feuille de plus, a la fin (pas de feuilles dans un .csv).</summary>
	public Sheet AddSheet (string name)
	{
		if (_kind == Csv)
			throw new NotSupportedException ("Spreadsheet : un .csv n'a qu'une feuille");
		Sheet sheet;
		if (_kind == DK.DKK_XLSX)
		{
			int n = 1;
			while (_package.Has ("xl/worksheets/sheet" + n + ".xml"))
				n++;
			string part = "xl/worksheets/sheet" + n + ".xml";
			_package.Set (part, Ns.XmlHead + "<worksheet xmlns=\"" + Ns.S + "\"><sheetData/></worksheet>");
			var types = _package.Xml ("[Content_Types].xml").Root;
			types.Add (new XElement (Ns.Types + "Override", new XAttribute ("PartName", "/" + part),
				new XAttribute ("ContentType", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")));
			string relsPath = Doc.RelsPath (_workbook);
			var rels = _package.Xml (relsPath).Root;
			string id = "rId" + (rels.Elements ().Select (r => int.TryParse (((string) r.Attribute ("Id"))?.Substring (3), out int v) ? v : 0).DefaultIfEmpty ().Max () + 1);
			rels.Add (new XElement (Ns.Rels + "Relationship", new XAttribute ("Id", id),
				new XAttribute ("Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"), new XAttribute ("Target", "worksheets/sheet" + n + ".xml")));
			var book = _package.Xml (_workbook).Root.Element (Ns.S + "sheets");
			int sid = book.Elements ().Select (e => (int?) e.Attribute ("sheetId") ?? 0).DefaultIfEmpty ().Max () + 1;
			var e = new XElement (Ns.S + "sheet", new XAttribute ("name", name), new XAttribute ("sheetId", sid), new XAttribute (Ns.R + "id", id));
			book.Add (e);
			sheet = new Sheet (this, name, _package.Xml (part).Root.Element (Ns.S + "sheetData")) { Rename = v => e.SetAttributeValue ("name", v) };
		}
		else
		{
			var body = _package.Xml ("content.xml").Root.Element (Ns.Office + "body").Element (Ns.Office + "spreadsheet");
			var e = new XElement (Ns.Table + "table", new XAttribute (Ns.Table + "name", name));
			body.Add (e);
			sheet = new Sheet (this, name, e) { Rename = v => e.SetAttributeValue (Ns.Table + "name", v) };
		}
		_sheets.Add (sheet);
		return sheet;
	}

	void ReadXlsx ()
	{
		_workbook = Ns.MainPart (_package, "xl/workbook.xml");
		string rels = Doc.RelsPath (_workbook);
		var shared = new List<string> ();
		string strings = _package.Has (rels) ? Doc.TargetOfType (_package.Bytes (rels), "sharedStrings") : null;
		if (!string.IsNullOrEmpty (strings))
		{
			string part = strings.StartsWith ('/') ? strings.TrimStart ('/') : Doc.PartPath (_workbook, strings);
			foreach (var si in _package.Xml (part).Root.Elements (Ns.S + "si"))
				shared.Add (string.Concat (si.Descendants (Ns.S + "t").Where (t => t.Parent.Name != Ns.S + "rPh").Select (t => t.Value)));
		}
		foreach (var e in _package.Xml (_workbook).Root.Element (Ns.S + "sheets").Elements (Ns.S + "sheet"))
		{
			var entry = e;
			string part = Ns.Target (_package, _workbook, (string) e.Attribute (Ns.R + "id"));
			var data = _package.Xml (part).Root.Element (Ns.S + "sheetData");
			var sheet = new Sheet (this, (string) e.Attribute ("name"), data) { Rename = v => entry.SetAttributeValue ("name", v) };
			foreach (var c in data.Descendants (Ns.S + "c"))
			{
				var cell = sheet.Cells[(string) c.Attribute ("r")];
				cell.Xlsx = c;
				cell.RawFormula = (string) c.Element (Ns.S + "f");
				string v = (string) c.Element (Ns.S + "v");
				cell.Raw = (string) c.Attribute ("t") switch
				{
					"s" => shared[int.Parse (v, CultureInfo.InvariantCulture)],
					"inlineStr" => string.Concat (c.Element (Ns.S + "is").Descendants (Ns.S + "t").Select (t => t.Value)),
					"str" or "e" => v,
					"b" => v == "1",
					_ => v == null ? null : double.Parse (v, CultureInfo.InvariantCulture),
				};
			}
			_sheets.Add (sheet);
		}
	}

	void ReadOds ()
	{
		var body = _package.Xml ("content.xml").Root.Element (Ns.Office + "body").Element (Ns.Office + "spreadsheet");
		foreach (var table in body.Elements (Ns.Table + "table"))
		{
			var entry = table;
			var sheet = new Sheet (this, (string) table.Attribute (Ns.Table + "name"), table) { Rename = v => entry.SetAttributeValue (Ns.Table + "name", v) };
			int row = 0;
			foreach (var r in table.Descendants (Ns.Table + "table-row"))
			{
				int rows = (int?) r.Attribute (Ns.Table + "number-rows-repeated") ?? 1, column = 0;
				foreach (var c in r.Elements ().Where (e => e.Name == Ns.Table + "table-cell" || e.Name == Ns.Table + "covered-table-cell"))
				{
					int columns = (int?) c.Attribute (Ns.Table + "number-columns-repeated") ?? 1;
					object value = OdsValue (c);
					if (value != null)
						for (int i = 0; i < rows; i++)
							for (int j = 0; j < columns; j++)
							{
								var cell = sheet.Cells[row + i, column + j];
								cell.Raw = value;
								cell.RawFormula = (string) c.Attribute (Ns.Table + "formula");
								cell.OdfStyle = (string) c.Attribute (Ns.Table + "style-name");
							}
					column += columns;
				}
				row += rows;
			}
			_sheets.Add (sheet);
		}
	}

	static object OdsValue (XElement c)
	{
		string type = (string) c.Attribute (Ns.Office + "value-type");
		if (type == null)
			return null;
		if (type == "float" || type == "percentage" || type == "currency")
			return double.Parse ((string) c.Attribute (Ns.Office + "value"), CultureInfo.InvariantCulture);
		if (type == "boolean")
			return (string) c.Attribute (Ns.Office + "boolean-value") == "true";
		return string.Join ("\n", c.Elements (Ns.Text + "p").Select (p => p.Value));
	}

	unsafe void ReadCsv (byte[] file)
	{
		var sheet = new Sheet (this, System.IO.Path.GetFileNameWithoutExtension (_path), null);
		byte[] text = new byte[file.Length + 1];
		fixed (byte* f = file, t = text)
		{
			long reader = DK.dk_csv_open ((long) f, file.Length, 0);
			_separator = (char) DK.dk_csv_sep (reader);
			int row = 0, column = 0;
			long ev;
			while ((ev = DK.dk_csv_next (reader)) != 0)
			{
				string s = Encoding.UTF8.GetString (text, 0, (int) DK.dk_csv_text (reader, (long) t, text.Length));
				if (s.Length > 0)
					sheet.Cells[row, column].Raw = double.TryParse (s, NumberStyles.Float, CultureInfo.InvariantCulture, out double d) ? d : s;
				column++;
				if ((ev & DK.DKV_EOL) != 0)
				{
					row++;
					column = 0;
				}
			}
			DK.dk_csv_close (reader);
		}
		_sheets.Add (sheet);
	}

	internal void FormulaGone () => _noCalcChain = true;

	// (une cellule de calcChain.xml sans formule : Excel refuse le fichier)
	void DropCalcChain ()
	{
		string rels = Doc.RelsPath (_workbook);
		var r = _package.Has (rels) ? _package.Xml (rels).Root : null;
		var link = r?.Elements ().FirstOrDefault (e => ((string) e.Attribute ("Type"))?.EndsWith ("/calcChain") == true);
		if (link == null)
			return;
		string part = Doc.PartPath (_workbook, (string) link.Attribute ("Target"));
		link.Remove ();
		_package.Remove (part);
		_package.Xml ("[Content_Types].xml").Root.Elements ().Where (e => (string) e.Attribute ("PartName") == "/" + part).Remove ();
	}

	static void WriteXlsx (Sheet sheet, Cell cell)
	{
		var data = sheet.Xml;
		var c = cell.Xlsx;
		if (c == null)
		{
			string number = (cell.Row + 1).ToString (CultureInfo.InvariantCulture);
			var row = data.Elements (Ns.S + "row").FirstOrDefault (e => (int) e.Attribute ("r") >= cell.Row + 1);
			if (row == null || (int) row.Attribute ("r") != cell.Row + 1)
			{
				var fresh = new XElement (Ns.S + "row", new XAttribute ("r", number));
				if (row == null)
					data.Add (fresh);
				else
					row.AddBeforeSelf (fresh);
				row = fresh;
			}
			c = new XElement (Ns.S + "c", new XAttribute ("r", cell.Address));
			var after = row.Elements (Ns.S + "c").FirstOrDefault (e => Spreadsheet.Parse ((string) e.Attribute ("r")).Column > cell.Column);
			if (after == null)
				row.Add (c);
			else
				after.AddBeforeSelf (c);
			cell.Xlsx = c;
		}
		c.Elements ().Remove ();
		c.Attribute ("t")?.Remove ();
		switch (cell.Raw)
		{
		case double d:
			c.Add (new XElement (Ns.S + "v", d.ToString ("R", CultureInfo.InvariantCulture)));
			break;
		case bool b:
			c.SetAttributeValue ("t", "b");
			c.Add (new XElement (Ns.S + "v", b ? "1" : "0"));
			break;
		case string s:
			c.SetAttributeValue ("t", "inlineStr");
			var t = new XElement (Ns.S + "t", s);
			t.SetAttributeValue (XNamespace.Xml + "space", "preserve");
			c.Add (new XElement (Ns.S + "is", t));
			break;
		}
	}

	// Une feuille ODS changee est recrite ligne a ligne : ses cellules gardent style et formule
	static void WriteOds (Sheet sheet)
	{
		XName rowName = Ns.Table + "table-row", cellName = Ns.Table + "table-cell";
		sheet.Xml.Descendants (rowName).ToList ().ForEach (r => r.Remove ());
		sheet.Xml.Elements ().Where (e => e.Name == Ns.Table + "table-header-rows" || e.Name == Ns.Table + "table-rows").Remove ();
		var cells = sheet.Cells.Map.Values.Where (c => c.Raw != null).ToLookup (c => c.Row);
		for (int row = 0, rows = sheet.RowCount; row < rows; row++)
		{
			var e = new XElement (rowName);
			int column = 0;
			foreach (var c in cells[row].OrderBy (c => c.Column))
			{
				if (c.Column > column)
					e.Add (new XElement (cellName, new XAttribute (Ns.Table + "number-columns-repeated", c.Column - column)));
				var x = new XElement (cellName);
				if (c.OdfStyle != null)
					x.SetAttributeValue (Ns.Table + "style-name", c.OdfStyle);
				if (c.RawFormula != null)
					x.SetAttributeValue (Ns.Table + "formula", c.RawFormula);
				switch (c.Raw)
				{
				case double d:
					x.SetAttributeValue (Ns.Office + "value-type", "float");
					x.SetAttributeValue (Ns.Office + "value", d.ToString ("R", CultureInfo.InvariantCulture));
					break;
				case bool b:
					x.SetAttributeValue (Ns.Office + "value-type", "boolean");
					x.SetAttributeValue (Ns.Office + "boolean-value", b ? "true" : "false");
					break;
				default:
					x.SetAttributeValue (Ns.Office + "value-type", "string");
					break;
				}
				x.Add (new XElement (Ns.Text + "p", c.Text));
				e.Add (x);
				column = c.Column + 1;
			}
			sheet.Xml.Add (e);
		}
	}

	void SaveCsv ()
	{
		var b = new StringBuilder ();
		var sheet = _sheets[0];
		for (int r = 0, rows = sheet.RowCount, columns = sheet.ColumnCount; r < rows; r++)
		{
			for (int c = 0; c < columns; c++)
			{
				if (c > 0)
					b.Append (_separator);
				string s = sheet.Cells.Map.TryGetValue ((r, c), out var cell) ? cell.Text : "";
				if (s.IndexOfAny (new[] { _separator, '"', '\n', '\r' }) >= 0)
					s = "\"" + s.Replace ("\"", "\"\"") + "\"";
				b.Append (s);
			}
			b.Append ('\n');
		}
		File.WriteAllText (_path, b.ToString (), new UTF8Encoding (false));
	}

	/// <summary>Enregistre ; path : un autre fichier, qui devient le sien. Le format ne change pas.</summary>
	public void Save (string path = null)
	{
		_path = path ?? _path ?? throw new InvalidOperationException ("Spreadsheet : pas de chemin");
		if (_kind == Csv)
		{
			SaveCsv ();
			return;
		}
		foreach (var sheet in _sheets)
		{
			if (_kind == DK.DKK_XLSX)
				sheet.Changed.ForEach (c => WriteXlsx (sheet, c));
			else if (sheet.Changed.Count > 0)
				WriteOds (sheet);
			sheet.Changed.Clear ();
		}
		if (_noCalcChain && _kind == DK.DKK_XLSX)
			DropCalcChain ();
		_noCalcChain = false;
		_package.Save (_path);
	}
}

/// <summary>Une diapositive. Title : son titre, ou son premier paragraphe.</summary>
public sealed class Slide
{
	public IReadOnlyList<Paragraph> Paragraphs { get; }
	public Paragraph TitleParagraph { get; }
	public string Title => TitleParagraph?.Text ?? "";
	internal Slide (List<Paragraph> paragraphs, Paragraph title) { Paragraphs = paragraphs; TitleParagraph = title ?? paragraphs.FirstOrDefault (); }
}

/// <summary>Une presentation : .pptx ou .odp, a lire et a corriger (pas de creation : une presentation vide
/// demande un masque).</summary>
public sealed class Presentation
{
	readonly Package _package;
	readonly List<Slide> _slides = new ();
	string _path;
	public string Path => _path;
	public IReadOnlyList<Slide> Slides => _slides;

	Presentation (Package package, long kind, string path)
	{
		_package = package;
		_path = path;
		if (kind == DK.DKK_PPTX)
		{
			string main = Ns.MainPart (package, "ppt/presentation.xml");
			foreach (var id in package.Xml (main).Root.Element (Ns.P + "sldIdLst")?.Elements (Ns.P + "sldId") ?? Enumerable.Empty<XElement> ())
			{
				var root = package.Xml (Ns.Target (package, main, (string) id.Attribute (Ns.R + "id"))).Root;
				var title = root.Descendants (Ns.P + "sp").FirstOrDefault (s =>
					s.Descendants (Ns.P + "ph").Any (h => (string) h.Attribute ("type") is "title" or "ctrTitle"));
				var paragraphs = root.Descendants (Ns.A + "p").Select (e => new Paragraph (e, Dialect.Drawing)).ToList ();
				var first = title?.Descendants (Ns.A + "p").FirstOrDefault ();
				_slides.Add (new Slide (paragraphs, paragraphs.FirstOrDefault (p => p.Element == first)));
			}
		}
		else
		{
			var body = package.Xml ("content.xml").Root.Element (Ns.Office + "body").Element (Ns.Office + "presentation");
			foreach (var page in body.Elements (Ns.Draw + "page"))
			{
				var title = page.Descendants (Ns.Draw + "frame").FirstOrDefault (f => (string) f.Attribute (Ns.Presentation + "class") == "title");
				var paragraphs = page.Descendants (Ns.Text + "p").Select (e => new Paragraph (e, Dialect.Odf)).ToList ();
				var first = title?.Descendants (Ns.Text + "p").FirstOrDefault ();
				_slides.Add (new Slide (paragraphs, paragraphs.FirstOrDefault (p => p.Element == first)));
			}
		}
	}

	public static Presentation Open (string path)
	{
		long kind = Ns.Kind (path);
		if (kind != DK.DKK_PPTX && kind != DK.DKK_ODP)
			throw new NotSupportedException ("Presentation : ni .pptx ni .odp : " + path);
		return new Presentation (new Package (path), kind, path);
	}

	/// <summary>Enregistre ; path : un autre fichier, qui devient le sien.</summary>
	public void Save (string path = null)
	{
		_path = path ?? _path;
		_package.Save (_path);
	}
}
