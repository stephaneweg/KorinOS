// Widgets.cs -- les controles composes de System.Windows.Forms : onglets, volets, barre d'outils,
// arbre, grille, date, couleur. Chacun est un widget d'UIKit (docs/DOTNET.md).
#nullable disable
using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Kits;
using System.Reflection;
using System.Runtime.InteropServices;
using System.UIKit;

namespace System.Windows.Forms;

public enum Orientation { Horizontal, Vertical }

/// <summary>Une page : le panneau que ui_tabs a fait pour elle.</summary>
public class TabPage : Panel
{
	public TabPage () { }
	public TabPage (string text) { Text = text; }
	internal override bool Attached => true;
	internal override long Create (int x, int y, int w, int h) => UI.ui_tabs_page (Parent.Handle, Parent.Controls.IndexOf (this));
	protected override void WriteText (string s) { }	// (UIKit ne change pas un onglet apres coup)
}

public class TabControl : Control
{
	int _selected;
	static int Header => Metrics.ControlHeight + 8;	// (ui_tabs : l'en-tete, puis 8)
	public ControlCollection TabPages => Controls;
	public TabPage SelectedTab => SelectedIndex >= 0 && SelectedIndex < Controls.Count ? (TabPage) Controls[SelectedIndex] : null;
	public event EventHandler SelectedIndexChanged;
	public int SelectedIndex
	{
		get => Created ? (int) UI.ui_w_get_value (Handle) : _selected;
		set { _selected = value; if (Created) UI.ui_w_set_value (Handle, value); SyncPages (); }
	}

	/// <summary>Cachees d'abord : celle qui part rend ce que celle qui arrive va prendre (la surface d'un Viewport3D).</summary>
	void SyncPages ()
	{
		int s = SelectedIndex;
		for (int i = 0; i < Controls.Count; i++)
			if (i != s)
				Controls[i].PageHidden (true);
		if (s >= 0 && s < Controls.Count)
			Controls[s].PageHidden (false);
	}

	internal override long Create (int x, int y, int w, int h)
	{
		var names = new List<string> ();
		foreach (var c in Controls.Items)
			names.Add (c.Text);
		return Ui.With (string.Join ("|", names), t => UI.ui_tabs (x, y, w, h, t));
	}

	internal override void CreateTree ()
	{
		SyncPages ();
		base.CreateTree ();
		if (_selected != 0)
			UI.ui_w_set_value (Handle, _selected);
	}

	internal override void LayoutChildren ()
	{
		if (!Ui.Ready)
			return;
		foreach (var c in Controls.Items)
			c.SetBounds (0, Header, Width, Height - Header);
	}

	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
		{
			SyncPages ();
			SelectedIndexChanged?.Invoke (this, EventArgs.Empty);
		}
	}
}

public class SplitterPanel : Panel { }

/// <summary>Deux volets et leur poignee : ui_splitter rend une position dans la fenetre.</summary>
public class SplitContainer : Control
{
	sealed class Bar : Control
	{
		internal SplitContainer Owner;
		long Origin (Point p) => Owner.Orientation == Orientation.Vertical ? p.X - Owner._distance : p.Y - Owner._distance;
		long Min (Point p) => Origin (p) + Owner.Panel1MinSize;
		long Max (Point p) => Origin (p) + Owner.Length - Owner.Panel2MinSize - Owner.SplitterWidth;

		internal override long Create (int x, int y, int w, int h)
		{
			var p = new Point (x, y);
			return UI.ui_splitter (x, y, w, h, Min (p), Max (p));
		}

		internal void Track ()
		{
			if (!Created)
				return;
			var p = Absolute ();
			UI.ui_w_set_range (Handle, Min (p), Max (p), 0, 1);
		}

		internal override void Notify (long code)
		{
			if (code == UI.UN_CHANGE)
				Owner.Dragged ((int) (UI.ui_w_get_value (Handle) - Origin (Absolute ())));
		}
	}

	readonly Bar _bar;
	int _distance = 120;
	public SplitterPanel Panel1 { get; } = new ();
	public SplitterPanel Panel2 { get; } = new ();
	public Orientation Orientation { get; set; } = Orientation.Vertical;
	public int SplitterWidth { get; set; } = 6;
	public int Panel1MinSize { get; set; } = 40;
	public int Panel2MinSize { get; set; } = 40;
	public event EventHandler SplitterMoved;
	int Length => Orientation == Orientation.Vertical ? Width : Height;
	bool _collapsed;

	/// <summary>Le panneau 2 replie : le panneau 1 prend toute la place, sans barre.</summary>
	public bool Panel2Collapsed
	{
		get => _collapsed;
		set
		{
			if (_collapsed == value)
				return;
			_collapsed = value;
			Panel2.Visible = !value;
			_bar.Visible = !value;
			LayoutChildren ();
			Application.Redraw ();
		}
	}

	public SplitContainer ()
	{
		_bar = new Bar { Owner = this };
		Controls.Add (Panel1);
		Controls.Add (Panel2);
		Controls.Add (_bar);
	}

	public int SplitterDistance
	{
		get => _distance;
		set { _distance = value; LayoutChildren (); Application.Redraw (); }
	}

	void Dragged (int distance)
	{
		_distance = distance;
		LayoutChildren ();
		Application.Redraw ();
		SplitterMoved?.Invoke (this, EventArgs.Empty);
	}

	internal override long Create (int x, int y, int w, int h) => UI.ui_panel (x, y, w, h, 0);

	internal override void LayoutChildren ()
	{
		int s = SplitterWidth;
		if (_collapsed)
		{
			Panel1.SetBounds (0, 0, Width, Height);
			return;
		}
		if (Length > Panel1MinSize + Panel2MinSize + s)
			_distance = Math.Clamp (_distance, Panel1MinSize, Length - Panel2MinSize - s);
		if (Orientation == Orientation.Vertical)
		{
			Panel1.SetBounds (0, 0, _distance, Height);
			_bar.SetBounds (_distance, 0, s, Height);
			Panel2.SetBounds (_distance + s, 0, Width - _distance - s, Height);
		}
		else
		{
			Panel1.SetBounds (0, 0, Width, _distance);
			_bar.SetBounds (0, _distance, Width, s);
			Panel2.SetBounds (0, _distance + s, Width, Height - _distance - s);
		}
		_bar.Track ();
	}
}

/// <summary>Un bouton de barre d'outils : Glyph, un caractere de DejaVu Sans (« ⚙ ») ; Text, un libelle.</summary>
public abstract class ToolStripItem : Control
{
	internal override bool Attached => true;
}

public class ToolStripButton : ToolStripItem
{
	bool _checked;
	public string Glyph { get; set; }
	public bool CheckOnClick { get; set; }
	public bool Checked
	{
		get => Created ? UI.ui_w_get_value (Handle) != 0 : _checked;
		set { _checked = value; if (Created) UI.ui_w_set_value (Handle, value ? 1 : 0); }
	}
	public ToolStripButton () { }
	public ToolStripButton (string text) { Text = text; }

	internal override long Create (int x, int y, int w, int h)
	{
		var bar = (ToolStrip) Parent;
		long flags = (CheckOnClick ? UI.UTB_TOGGLE : 0) | (bar.TakeSeparator () ? UI.UTB_SEP : 0);
		long g = Glyph == null ? 0 : KitLoader.Utf8 (Glyph);
		long t = RawText.Length == 0 ? 0 : KitLoader.Utf8 (RawText);
		try { return UI.ui_toolbar_add (bar.Handle, g, t, flags); }
		finally
		{
			if (g != 0) KitLoader.Free (g);
			if (t != 0) KitLoader.Free (t);
		}
	}
}

public class ToolStripSeparator : ToolStripItem
{
	internal override void CreateTree () => ((ToolStrip) Parent).SeparatorNext = true;
}

public class ToolStrip : Control
{
	internal bool SeparatorNext;
	public ControlCollection Items => Controls;
	public ToolStrip () { Dock = DockStyle.Top; AutoSize = true; }
	internal bool TakeSeparator () { bool b = SeparatorNext; SeparatorNext = false; return b; }
	public override Size GetPreferredSize (Size proposed) => new Size (Width, Metrics.ControlHeight + 16);
	internal override long Create (int x, int y, int w, int h) => UI.ui_toolbar (x, y, w, 0);
	internal override void LayoutChildren () { }	// (UIKit place ses boutons)
}

public class TreeNodeCollection : List<TreeNode>
{
	readonly TreeNode _owner;
	readonly TreeView _tree;
	internal TreeNodeCollection (TreeNode owner, TreeView tree) { _owner = owner; _tree = tree; }
	public new TreeNode Add (TreeNode n)
	{
		n.Parent = _owner;
		n.Tree = _tree ?? _owner?.Tree;
		base.Add (n);
		n.Realize ();
		return n;
	}
	public TreeNode Add (string text) => Add (new TreeNode (text));
}

public class TreeNode
{
	internal long Id = -1;
	internal TreeView Tree;
	bool _expanded;
	public string Text { get; set; }
	public object Tag { get; set; }
	public TreeNode Parent { get; internal set; }
	public TreeNodeCollection Nodes { get; }
	public TreeNode (string text = "") { Text = text; Nodes = new TreeNodeCollection (this, null); }

	internal void Realize ()
	{
		if (Tree == null || !Tree.Created || Id >= 0 || (Parent != null && Parent.Id < 0))
			return;
		long parent = Parent?.Id ?? -1;
		Id = Ui.With (Text, t => UI.ui_tree_add (Tree.Handle, parent, t, 0));
		Tree.Ids[Id] = this;
		foreach (var n in Nodes)
		{
			n.Tree = Tree;
			n.Realize ();
		}
		if (_expanded)
			UI.ui_tree_expand (Tree.Handle, Id, 1);
	}

	public void Expand () { _expanded = true; if (Id >= 0) UI.ui_tree_expand (Tree.Handle, Id, 1); }
	public void Collapse () { _expanded = false; if (Id >= 0) UI.ui_tree_expand (Tree.Handle, Id, 0); }
	public void ExpandAll ()
	{
		Expand ();
		foreach (var n in Nodes)
			n.ExpandAll ();
	}
}

public class TreeViewEventArgs : EventArgs
{
	public TreeNode Node { get; }
	public TreeViewEventArgs (TreeNode node) { Node = node; }
}
public delegate void TreeViewEventHandler (object sender, TreeViewEventArgs e);

public class TreeView : Control
{
	internal readonly Dictionary<long, TreeNode> Ids = new ();
	public TreeNodeCollection Nodes { get; }
	public event TreeViewEventHandler AfterSelect;
	public event TreeViewEventHandler NodeActivated;	// (double-clic ou Entree sur une feuille)
	public TreeView () { Nodes = new TreeNodeCollection (null, this); Size = new Size (160, 120); }

	public TreeNode SelectedNode
	{
		get => Created && Ids.TryGetValue (UI.ui_w_get_value (Handle), out var n) ? n : null;
		set { if (Created && value?.Id >= 0) UI.ui_w_set_value (Handle, value.Id); }
	}

	public void ExpandAll ()
	{
		foreach (var n in Nodes)
			n.ExpandAll ();
	}

	public void Clear ()
	{
		Nodes.Clear ();
		Ids.Clear ();
		if (Created)
			UI.ui_tree_clear (Handle);
	}

	internal override long Create (int x, int y, int w, int h) => UI.ui_tree (x, y, w, h);

	internal override void CreateTree ()
	{
		base.CreateTree ();
		foreach (var n in Nodes)
			n.Realize ();
	}

	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			AfterSelect?.Invoke (this, new TreeViewEventArgs (SelectedNode));
		else if (code == UI.UN_ACTIVATE)
			NodeActivated?.Invoke (this, new TreeViewEventArgs (SelectedNode));
	}
}

public class DataGridViewColumn
{
	public string HeaderText { get; set; } = "";
	public int Width { get; set; } = 100;
	public DataGridViewColumn () { }
	public DataGridViewColumn (string header, int width = 100) { HeaderText = header; Width = width; }
}

public class DataGridViewRow
{
	public object[] Cells { get; }
	public object Tag { get; set; }
	public DataGridViewRow (object[] cells) { Cells = cells; }
	internal string Text => string.Join ("\t", Array.ConvertAll (Cells, c => Convert.ToString (c, CultureInfo.CurrentCulture)?.Replace ('\t', ' ')));
}

public class DataGridViewRowCollection : List<DataGridViewRow>
{
	readonly DataGridView _owner;
	internal DataGridViewRowCollection (DataGridView owner) { _owner = owner; }
	public new void Add (DataGridViewRow r) { base.Add (r); _owner.RefreshRows (); }
	public void Add (params object[] cells) => Add (new DataGridViewRow (cells));
	public new void RemoveAt (int i) { base.RemoveAt (i); _owner.RefreshRows (); }
	public new void Clear () { base.Clear (); _owner.RefreshRows (); }
}

/// <summary>En lecture seule : une liste virtuelle d'UIKit, dont les cellules ne se saisissent pas.</summary>
public unsafe class DataGridView : Control
{
	object _source;
	static long s_text;
	public List<DataGridViewColumn> Columns { get; } = new ();
	public DataGridViewRowCollection Rows { get; }
	public event EventHandler SelectionChanged;
	public event EventHandler RowActivated;		// (double-clic ou Entree)
	public DataGridView () { Rows = new DataGridViewRowCollection (this); Size = new Size (240, 120); }

	public int CurrentRowIndex => Created ? (int) UI.ui_w_get_value (Handle) : -1;
	public DataGridViewRow CurrentRow => CurrentRowIndex >= 0 && CurrentRowIndex < Rows.Count ? Rows[CurrentRowIndex] : null;

	/// <summary>Une ligne par element, une colonne par propriete publique.</summary>
	public object DataSource
	{
		get => _source;
		set
		{
			_source = value;
			Rows.Clear ();
			if (value is not IEnumerable all)
				return;
			foreach (var o in all)
			{
				var props = o.GetType ().GetProperties (BindingFlags.Instance | BindingFlags.Public);
				if (Columns.Count == 0)
					foreach (var p in props)
						Columns.Add (new DataGridViewColumn (p.Name));
				var cells = new object[props.Length];
				for (int i = 0; i < props.Length; i++)
					cells[i] = props[i].GetValue (o);
				Rows.Add (cells);
			}
		}
	}

	internal override long Create (int x, int y, int w, int h)
	{
		long l = UI.ui_list (x, y, w, h, UI.UL_HEADER | UI.UL_STRIPES | UI.UL_GRID | UI.UL_HSCROLL | UI.UL_USERSORT);
		if (l != 0)
			foreach (var c in Columns)
				Ui.With (c.HeaderText, t => UI.ui_list_column (l, t, c.Width, 0));
		return l;
	}

	internal override void CreateTree ()
	{
		base.CreateTree ();
		RefreshRows ();
	}

	internal void RefreshRows ()
	{
		if (!Created)
			return;
		delegate* unmanaged<long, long, long> fn = &RowText;
		UI.ui_list_virtual (Handle, Rows.Count, (long) fn);
		UI.ui_w_draw (Handle);
	}

	/// <summary>Le texte d'une ligne ; UIKit le lit tout de suite, il vit jusqu'a la suivante.</summary>
	[UnmanagedCallersOnly]
	static long RowText (long list, long row)
	{
		try
		{
			if (s_text != 0)
				KitLoader.Free (s_text);
			var g = (DataGridView) Application.Map[list];
			s_text = KitLoader.Utf8 (row >= 0 && row < g.Rows.Count ? g.Rows[(int) row].Text : "");
			return s_text;
		}
		catch (Exception e)
		{
			Application.Fail (e);
			return 0;
		}
	}

	static int Compare (object a, object b)
	{
		if (a == null || b == null)
			return (a == null ? 0 : 1) - (b == null ? 0 : 1);
		if (a.GetType () == b.GetType () && a is IComparable c)
			return c.CompareTo (b);
		return string.Compare (a.ToString (), b.ToString (), StringComparison.CurrentCulture);
	}

	void SortBy (int column, bool descending)
	{
		var sorted = new List<DataGridViewRow> (Rows);
		var order = new List<(DataGridViewRow row, int at)> ();
		for (int i = 0; i < sorted.Count; i++)
			order.Add ((sorted[i], i));
		order.Sort ((a, b) =>
		{
			int r = Compare (column < a.row.Cells.Length ? a.row.Cells[column] : null, column < b.row.Cells.Length ? b.row.Cells[column] : null);
			return r != 0 ? (descending ? -r : r) : a.at - b.at;
		});
		((List<DataGridViewRow>) Rows).Clear ();
		foreach (var o in order)
			((List<DataGridViewRow>) Rows).Add (o.row);
		RefreshRows ();
	}

	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
		{
			SelectionChanged?.Invoke (this, EventArgs.Empty);
		}
		else if (code == UI.UN_ACTIVATE)
		{
			RowActivated?.Invoke (this, EventArgs.Empty);
		}
		else if (code == UI.UN_SORT)
		{
			UI.ui_list_sorted (Handle, out long column, out long descending);
			SortBy ((int) column, descending != 0);
		}
	}
}

public class DateTimePicker : Control
{
	DateTime _value = DateTime.Today;
	public event EventHandler ValueChanged;
	public DateTimePicker () { Size = new Size (140, 28); }

	static long Encode (DateTime d) => (long) d.Year << 16 | (long) d.Month << 8 | (long) d.Day;

	public DateTime Value
	{
		get
		{
			if (Created)
			{
				long v = UI.ui_w_get_value (Handle);
				return new DateTime ((int) (v >> 16), (int) (v >> 8) & 255, (int) v & 255);
			}
			return _value;
		}
		set { _value = value; if (Created) UI.ui_w_set_value (Handle, Encode (value)); }
	}

	internal override long Create (int x, int y, int w, int h)
	{
		long d = UI.ui_datepicker (x, y, w, h, Encode (_value));
		if (d != 0)
			UI.ui_datepicker_today (d, Encode (DateTime.Today));
		return d;
	}

	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			ValueChanged?.Invoke (this, EventArgs.Empty);
	}
}

public class ColorDialog : IDisposable
{
	public Color Color { get; set; } = Color.Black;
	public DialogResult ShowDialog ()
	{
		Ui.Init ();
		long r = Ui.With ("Couleur", t => UI.ui_color_dialog (t, Color.ToArgb () & 0xFFFFFF));
		if (r < 0)
			return DialogResult.Cancel;
		Color = Color.FromArgb (unchecked ((int) (0xFF000000 | (uint) r)));
		return DialogResult.OK;
	}
	public void Dispose () { }
}

/// <summary>Un choix parmi quelques segments, sans liste qui s'ouvre.</summary>
public class SegmentedControl : ListControl
{
	public SegmentedControl () { Size = new Size (200, 28); }
	internal override long Create (int x, int y, int w, int h) => Ui.With (string.Join ("|", Items), t => UI.ui_segmented (x, y, w, h, t));
}

/// <summary>Barre verticale : Value va de Minimum a Maximum - LargeChange + 1, comme WinForms.</summary>
public class VScrollBar : Control
{
	int _value, _max = 100, _large = 10;
	public event EventHandler Scroll;
	public int Minimum { get; set; }
	public int Maximum { get => _max; set { _max = value; Range (); } }
	public int LargeChange { get => _large; set { _large = value; Range (); } }
	public int Value
	{
		get => Created ? (int) UI.ui_w_get_value (Handle) : _value;
		set { _value = value; if (Created) UI.ui_w_set_value (Handle, value); }
	}
	public VScrollBar () { Size = new Size (16, 100); }
	void Range () { if (Created) UI.ui_w_set_range (Handle, Minimum, Math.Max (Minimum, _max - _large + 1), _large, 1); }
	internal override long Create (int x, int y, int w, int h) => UI.ui_scrollbar (x, y, w, h, 0);
	internal override void CreateTree ()
	{
		base.CreateTree ();
		Range ();
		UI.ui_w_set_value (Handle, _value);
	}
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			Scroll?.Invoke (this, EventArgs.Empty);
	}
}

/// <summary>Un menu au point d'un clic ; « - » fait un trait.</summary>
public class ContextMenuStrip
{
	public List<ToolStripMenuItem> Items { get; } = new ();
	public ToolStripMenuItem Add (string text, Action click = null)
	{
		var m = new ToolStripMenuItem (text);
		if (click != null)
			m.Click += (s, e) => click ();
		Items.Add (m);
		return m;
	}
	public void Show (Control c, Point at)
	{
		if (Items.Count == 0)
			return;
		var p = c.Absolute ();
		var b = new System.Text.StringBuilder ();
		int width = 80;
		foreach (var m in Items)
		{
			b.Append (m.Text).Append ('\0');
			width = Math.Max (width, Metrics.TextWidth (m.Text) + 70);
		}
		long i = Ui.With (b.ToString (), t => UI.ui_popup_local (p.X + at.X, p.Y + at.Y, t, width));
		if (i >= 0 && i < Items.Count)
			ToolStripMenuItem.Fire (Items[(int) i].Id);
	}
}

public static unsafe class Clipboard
{
	public static void SetText (string s) => Ui.With (s, p => UI.ui_clip_set (p, -1));
	public static string GetText ()
	{
		byte[] b = new byte[1 << 20];
		fixed (byte* p = b)
			return UI.ui_clip_get ((long) p, b.Length) > 0 ? KitLoader.String ((long) p) : "";
	}
}
