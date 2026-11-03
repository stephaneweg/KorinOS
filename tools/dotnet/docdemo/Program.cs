// docdemo : DocumentKit en C# sur un .docx fait par ZipArchive (docs/DOTNET.md).
//     pibuild bin /dotnet/test/docdemo docdemo
using System;
using System.DocumentKit;
using System.IO;
using System.IO.Compression;
using System.Text;

class Programme
{
	const string Rels = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		+ "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/a.png\"/>"
		+ "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"http://armpi/\" TargetMode=\"External\"/>"
		+ "</Relationships>";

	static byte[] Docx ()
	{
		var memory = new MemoryStream ();
		using (var zip = new ZipArchive (memory, ZipArchiveMode.Create, true))
			foreach (string name in new[] { "[Content_Types].xml", "_rels/.rels", "word/document.xml" })
				using (var w = new StreamWriter (zip.CreateEntry (name).Open ()))
					w.Write ("<x/>");
		return memory.ToArray ();
	}

	static int Main ()
	{
		int fails = 0;
		void Check (string name, object got, object want)
		{
			bool ok = Equals (got, want);
			Console.WriteLine ((ok ? "PASS " : "FAIL ") + name + " : " + got);
			if (!ok)
				fails++;
		}
		byte[] docx = Docx ();
		Check ("Kind", Doc.Kind (docx), DK.DKK_DOCX);
		Check ("Mime", Doc.KindOfMime (Doc.Mime (DK.DKK_XLSX)), DK.DKK_XLSX);
		Check ("RelsPath", Doc.RelsPath ("word/document.xml"), "word/_rels/document.xml.rels");
		Check ("PartPath", Doc.PartPath ("word/document.xml", "../media/a.png"), "media/a.png");
		var rels = Doc.Relations (Encoding.UTF8.GetBytes (Rels));
		Check ("Relations", rels.Length, 2);
		Check ("Type", rels[0].Type, "image");
		Check ("External", rels[1].External, true);
		Check ("TargetOfType", Doc.TargetOfType (Encoding.UTF8.GetBytes (Rels), "image"), "media/a.png");
		Check ("TargetOfType absent", Doc.TargetOfType (Encoding.UTF8.GetBytes (Rels), "chart"), null);
		var day = new DateOnly (2026, 10, 31);
		Check ("Excel", Doc.ToExcel (new DateOnly (2000, 1, 1)), 36526L);
		Check ("Excel retour", Doc.FromExcel (Doc.ToExcel (day)), day);
		Check ("Excel 1904", Doc.FromExcel (Doc.ToExcel (day, true), true), day);
		Check ("Longueur", Doc.ConvertLength (1, DK.DKU_IN, DK.DKU_TWIP), 1440L);
		Check ("Couleur", Doc.ParseColor ("#336699"), 0x336699L);
		Check ("Couleur autre", Doc.ParseColor ("rien", 7), 7L);
		var gz = new MemoryStream ();
		using (var g = new GZipStream (gz, CompressionMode.Compress, true))
			g.Write (docx);
		gz.Position = 0;
		var back = new MemoryStream ();
		new GZipStream (gz, CompressionMode.Decompress).CopyTo (back);
		Check ("GZipStream", back.Length, (long) docx.Length);
		Objects (Check);
		Console.WriteLine (fails == 0 ? "ok" : fails + " echec(s)");
		return fails;
	}

	static void Zip (string path, params (string Name, string Text)[] files)
	{
		File.Delete (path);
		using var zip = ZipFile.Open (path, ZipArchiveMode.Create);
		foreach (var (name, text) in files)
			using (var w = new StreamWriter (zip.CreateEntry (name).Open ()))
				w.Write (text);
	}

	// Le modele objet (Office.cs) : chaque format, ecrit puis relu, et un fichier fait ailleurs garde ce qu'on ne touche pas
	static void Objects (Action<string, object, object> check)
	{
		const string Dir = "/tmp/docdemo";
		Directory.CreateDirectory (Dir);
		const string Types = "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>";
		string Root (string target) => "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
			+ "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"" + target + "\"/></Relationships>";

		var d = Document.Create ();
		d.Add ("Bonjour", "Titre1");
		d.Add ("été à Noël\t<&>");
		d.Save (Dir + "/a.docx");
		d = Document.Open (Dir + "/a.docx");
		check ("docx lignes", d.Paragraphs.Count, 2);
		check ("docx style", d.Paragraphs[0].Style, "Titre1");
		check ("docx texte", d.Paragraphs[1].Text, "été à Noël\t<&>");
		d.Paragraphs[0].Text = "Salut";
		d.Remove (d.Paragraphs[1]);
		d.Add ("fin");
		d.Save ();
		check ("docx modifie", Document.Open (Dir + "/a.docx").Text, "Salut\nfin");

		const string W = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
		Zip (Dir + "/b.docx", ("[Content_Types].xml", Types), ("_rels/.rels", Root ("word/document.xml")), ("word/media/x.png", "image"),
			("word/document.xml", "<w:document " + W + "><w:body><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>un </w:t></w:r><w:r><w:t>deux</w:t></w:r><w:r><w:tab/><w:t>trois</w:t><w:br/></w:r></w:p>"
				+ "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>case</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:sectPr/></w:body></w:document>"));
		d = Document.Open (Dir + "/b.docx");
		check ("docx morceaux", d.Paragraphs[0].Text, "un deux\ttrois\n");
		check ("docx tableau", d.Paragraphs[1].Text, "case");
		d.Paragraphs[0].Text = "gras";
		d.Save ();
		using (var zip = ZipFile.OpenRead (Dir + "/b.docx"))
		{
			check ("docx image gardee", zip.GetEntry ("word/media/x.png") != null, true);
			using var r = new StreamReader (zip.GetEntry ("word/document.xml").Open ());
			string x = r.ReadToEnd ();
			check ("docx gras garde", x.Contains ("<w:b />") || x.Contains ("<w:b/>"), true);
			check ("docx sans BOM", x[0], '<');
		}

		d = Document.Create (DK.DKK_ODT);
		d.Add ("Texte ODT").Style = "Standard";
		d.Save (Dir + "/a.odt");
		check ("odt format", Doc.Kind (File.ReadAllBytes (Dir + "/a.odt")), DK.DKK_ODT);
		d = Document.Open (Dir + "/a.odt");
		check ("odt texte", d.Text, "Texte ODT");
		check ("odt style", d.Paragraphs[0].Style, "Standard");
		const string T = "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"";
		Zip (Dir + "/b.odt", ("mimetype", "application/vnd.oasis.opendocument.text"),
			("content.xml", "<office:document-content " + T + "><office:body><office:text><text:h>Titre</text:h><text:p>a<text:s text:c=\"2\"/>b<text:tab/><text:span>c</text:span><text:line-break/>d</text:p></office:text></office:body></office:document-content>"));
		d = Document.Open (Dir + "/b.odt");
		check ("odt titre", d.Paragraphs[0].Text, "Titre");
		check ("odt morceaux", d.Paragraphs[1].Text, "a  b\tc\nd");

		var s = Spreadsheet.Create ();
		var sheet = s.Sheets[0];
		sheet.Cells["B2"].Value = 42;
		sheet.Cells["A1"].Value = "texte";
		sheet.Cells["AA10"].Value = 1.5;
		sheet.Cells["C2"].Value = true;
		s.AddSheet ("Autre").Cells[0, 0].Value = "ici";
		s.Save (Dir + "/a.xlsx");
		check ("xlsx format", Doc.Kind (File.ReadAllBytes (Dir + "/a.xlsx")), DK.DKK_XLSX);
		s = Spreadsheet.Open (Dir + "/a.xlsx");
		check ("xlsx feuilles", s.Sheets.Count, 2);
		check ("xlsx nombre", s.Sheets[0].Cells["B2"].Value, 42.0);
		check ("xlsx texte", s.Sheets[0].Cells["A1"].Value, "texte");
		check ("xlsx booleen", s.Sheets[0].Cells["C2"].Value, true);
		check ("xlsx loin", s.Sheets[0].Cells[9, 26].Address, "AA10");
		check ("xlsx taille", (s.Sheets[0].RowCount, s.Sheets[0].ColumnCount), (10, 27));
		check ("xlsx nom", s["Autre"].Cells["A1"].Value, "ici");

		const string S = "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";
		const string R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
		Zip (Dir + "/b.xlsx", ("[Content_Types].xml", "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Override PartName=\"/xl/calcChain.xml\" ContentType=\"x\"/></Types>"),
			("_rels/.rels", Root ("xl/workbook.xml")),
			("xl/workbook.xml", "<workbook " + S + "><sheets><sheet name=\"Une\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"),
			("xl/_rels/workbook.xml.rels", "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"" + R + "worksheet\" Target=\"worksheets/sheet1.xml\"/>"
				+ "<Relationship Id=\"rId2\" Type=\"" + R + "sharedStrings\" Target=\"sharedStrings.xml\"/><Relationship Id=\"rId3\" Type=\"" + R + "calcChain\" Target=\"calcChain.xml\"/></Relationships>"),
			("xl/calcChain.xml", "<calcChain " + S + "/>"),
			("xl/sharedStrings.xml", "<sst " + S + "><si><t>commun</t></si><si><r><t>en </t></r><r><t>morceaux</t></r></si></sst>"),
			("xl/worksheets/sheet1.xml", "<worksheet " + S + "><sheetData><row r=\"1\"><c r=\"A1\" s=\"3\" t=\"s\"><v>0</v></c><c r=\"C1\" t=\"s\"><v>1</v></c></row>"
				+ "<row r=\"3\"><c r=\"B3\"><f>1+1</f><v>2</v></c></row></sheetData></worksheet>"));
		s = Spreadsheet.Open (Dir + "/b.xlsx");
		sheet = s.Sheets[0];
		check ("xlsx partage", sheet.Cells["A1"].Value, "commun");
		check ("xlsx morceaux", sheet.Cells["C1"].Value, "en morceaux");
		check ("xlsx formule", sheet.Cells["B3"].Formula + " = " + sheet.Cells["B3"].Text, "1+1 = 2");
		sheet.Cells["B3"].Value = 7;
		sheet.Cells["B1"].Value = "entre";
		sheet.Cells["A2"].Value = "ligne";
		sheet.Cells["A1"].Value = "style";
		s.Save ();
		using (var zip = ZipFile.OpenRead (Dir + "/b.xlsx"))
		{
			check ("xlsx calcChain retire", zip.GetEntry ("xl/calcChain.xml"), null);
			using var r = new StreamReader (zip.GetEntry ("xl/worksheets/sheet1.xml").Open ());
			string x = r.ReadToEnd ();
			check ("xlsx ordre", x.IndexOf ("r=\"A1\"") < x.IndexOf ("r=\"B1\"") && x.IndexOf ("r=\"B1\"") < x.IndexOf ("r=\"C1\"") && x.IndexOf ("r=\"C1\"") < x.IndexOf ("r=\"A2\"") && x.IndexOf ("r=\"A2\"") < x.IndexOf ("r=\"B3\""), true);
			check ("xlsx style garde", x.Contains ("s=\"3\""), true);
		}
		s = Spreadsheet.Open (Dir + "/b.xlsx");
		check ("xlsx relu", s.Sheets[0].Cells["B3"].Text + s.Sheets[0].Cells["B3"].Formula + s.Sheets[0].Cells["A1"].Text, "7style");

		s = Spreadsheet.Create (DK.DKK_ODS);
		s.Sheets[0].Cells["C2"].Value = 2.5;
		s.Sheets[0].Cells["A1"].Value = "ods";
		s.Sheets[0].Cells["B1"].Value = false;
		s.Save (Dir + "/a.ods");
		check ("ods format", Doc.Kind (File.ReadAllBytes (Dir + "/a.ods")), DK.DKK_ODS);
		s = Spreadsheet.Open (Dir + "/a.ods");
		check ("ods nombre", s.Sheets[0].Cells["C2"].Value, 2.5);
		check ("ods texte", s.Sheets[0].Cells["A1"].Value, "ods");
		check ("ods booleen", s.Sheets[0].Cells["B1"].Value, false);
		s.Sheets[0].Cells["C2"].Value = "fini";
		s.Save ();
		check ("ods relu", (Spreadsheet.Open (Dir + "/a.ods").Sheets[0].Cells["C2"].Value), "fini");

		File.WriteAllText (Dir + "/a.csv", "nom;age\n\"Du;pont\";42\n");
		s = Spreadsheet.Open (Dir + "/a.csv");
		check ("csv", s.Sheets[0].Cells["A2"].Text + " " + s.Sheets[0].Cells["B2"].Text, "Du;pont 42");
		s.Sheets[0].Cells["B2"].Value = 43;
		s.Save ();
		check ("csv relu", File.ReadAllText (Dir + "/a.csv"), "nom;age\n\"Du;pont\";43\n");

		const string P = "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";
		Zip (Dir + "/a.pptx", ("[Content_Types].xml", Types), ("_rels/.rels", Root ("ppt/presentation.xml")),
			("ppt/presentation.xml", "<p:presentation " + P + "><p:sldIdLst><p:sldId id=\"256\" r:id=\"rId1\"/></p:sldIdLst></p:presentation>"),
			("ppt/_rels/presentation.xml.rels", "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"" + R + "slide\" Target=\"slides/slide1.xml\"/></Relationships>"),
			("ppt/slides/slide1.xml", "<p:sld " + P + "><p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>corps</a:t></a:r></a:p></p:txBody></p:sp>"
				+ "<p:sp><p:nvSpPr><p:nvPr><p:ph type=\"title\"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:rPr lang=\"fr\"/><a:t>Le titre</a:t></a:r><a:endParaRPr/></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>"));
		var deck = Presentation.Open (Dir + "/a.pptx");
		check ("pptx", (deck.Slides.Count, deck.Slides[0].Title, deck.Slides[0].Paragraphs.Count), (1, "Le titre", 2));
		deck.Slides[0].TitleParagraph.Text = "Nouveau";
		deck.Save ();
		check ("pptx relu", Presentation.Open (Dir + "/a.pptx").Slides[0].Title, "Nouveau");
		Zip (Dir + "/a.odp", ("mimetype", "application/vnd.oasis.opendocument.presentation"),
			("content.xml", "<office:document-content " + T + " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\"><office:body><office:presentation>"
				+ "<draw:page><draw:frame><text:p>corps</text:p></draw:frame><draw:frame presentation:class=\"title\"><text:p>Titre ODP</text:p></draw:frame></draw:page></office:presentation></office:body></office:document-content>"));
		check ("odp", Presentation.Open (Dir + "/a.odp").Slides[0].Title, "Titre ODP");
	}
}
