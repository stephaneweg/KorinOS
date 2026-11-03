// Editing.cs -- ce qu'il faut a un editeur : le texte de code, les onglets de documents (ui_textarea,
// ui_tabstrip d'UIKit). Les couleurs de C# et de XML sont celles de ui_textarea (UTA_CS, UTA_XML).
#nullable disable
using System;
using System.Drawing;
using System.Kits;
using System.UIKit;

namespace System.Windows.Forms;

/// <summary>Les couleurs d'un CodeEditor.</summary>
public enum CodeLanguage { None, CSharp, Xml }

public class CodeEditor : Control
{
	public bool ReadOnly { get; set; }
	public bool LineNumbers { get; set; } = true;
	/// <summary>A donner avant la creation du controle.</summary>
	public CodeLanguage Language { get; set; }

	internal override long Create (int x, int y, int w, int h)
	{
		long s = UI.UTF_MONO | UI.UTA_VSCROLL | UI.UTA_HSCROLL | UI.UTA_TABS | (ReadOnly ? UI.UTF_READONLY : 0) | (LineNumbers ? UI.UTA_LINENUM : 0);
		if (Language == CodeLanguage.CSharp)
			s |= UI.UTA_CODE | UI.UTA_CS;
		else if (Language == CodeLanguage.Xml)
			s |= UI.UTA_CODE | UI.UTA_XML;
		long t = UI.ui_textarea (x, y, w, h, s);
		if (t != 0 && RawText.Length > 0)
			Ui.With (RawText, p => UI.ui_w_set_text (t, p));
		return t;
	}

	protected override string ReadText () => KitLoader.String (UI.ui_w_get_text (Handle));

	/// <summary>La ligne (0 : la premiere) et la colonne du curseur.</summary>
	public (int Line, int Column) Caret
	{
		get
		{
			if (!Created)
				return (0, 0);
			UI.ui_textarea_caret (Handle, out long line, out long column);
			return ((int) line, (int) column);
		}
	}

	/// <summary>Curseur au debut de cette ligne (0 : la premiere), visible.</summary>
	public void GoToLine (int line)
	{
		string s = Text;
		int at = 0;
		for (int i = 0; i < line && at >= 0; i++)
		{
			at = s.IndexOf ('\n', at);
			if (at >= 0)
				at++;
		}
		if (at >= 0 && Created)
			UI.ui_textarea_goto (Handle, System.Text.Encoding.UTF8.GetByteCount (s.AsSpan (0, at)));
	}

	/// <summary>La ligne eclairee (erreur) ; -1 : aucune.</summary>
	public void HighlightLine (int line) { if (Created) UI.ui_textarea_highlight (Handle, line); }
	public void Undo () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_UNDO); }
	public void Redo () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_REDO); }
	public void Cut () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_CUT); }
	public void Copy () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_COPY); }
	public void Paste () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_PASTE); }
	public void SelectAll () { if (Created) UI.ui_textarea_edit (Handle, UI.UTE_ALL); }
	public bool Find (string word) => Created && Ui.With (word, p => UI.ui_textarea_find (Handle, p)) != 0;
}

/// <summary>Barre d'onglets de documents : la croix demande, le programme ferme par RemoveAt.</summary>
public class DocumentTabs : Control
{
	public event EventHandler SelectedIndexChanged;
	public event EventHandler<int> CloseRequested;
	public event EventHandler NewRequested;		// (le « + »)
	/// <summary>Un « + » apres les onglets.</summary>
	public bool ShowNewButton { get; set; }
	public DocumentTabs () { Dock = DockStyle.Top; AutoSize = true; }

	public override Size GetPreferredSize (Size proposed) => new Size (Width, Metrics.ControlHeight + 4);
	internal override long Create (int x, int y, int w, int h) => UI.ui_tabstrip (x, y, w, h, UI.UTS_CLOSE | (ShowNewButton ? UI.UTS_PLUS : 0));
	internal override void LayoutChildren () { }

	public int Add (string title) => (int) Ui.With (title, t => UI.ui_tabstrip_add (Handle, t, 0));
	public void RemoveAt (int i) { UI.ui_list_remove (Handle, i); UI.ui_w_draw (Handle); }
	public void SetTitle (int i, string title) { Ui.With (title, t => UI.ui_list_set (Handle, i, t)); UI.ui_w_draw (Handle); }
	public int SelectedIndex
	{
		get => Created ? (int) UI.ui_w_get_value (Handle) : -1;
		set { if (Created) UI.ui_w_set_value (Handle, value); }
	}

	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			SelectedIndexChanged?.Invoke (this, EventArgs.Empty);
		else if (code == UI.UN_CLOSE)
			CloseRequested?.Invoke (this, (int) UI.ui_tabstrip_closing (Handle));
		else if (code == UI.UN_NEW)
			NewRequested?.Invoke (this, EventArgs.Empty);
	}
}
