// KVTable.cs -- les lignes cle / valeur a saisir (parametres, en-tetes, formulaires, tests, variables). UIKit n'a
// pas de grille modifiable : une vue qui dessine l'en-tete, et un jeu de vrais champs d'UIKit pour les lignes qui
// tiennent, relies aux donnees quand on defile. La derniere ligne est vide : y ecrire en ajoute une.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;

class KVTable : UserControl
{
	const int RowH = 32, HeadH = 24, BarW = 16, KindW = 52, MaxRows = 14;

	sealed class Row
	{
		public CheckBox On;
		public TextBox Key, Value, Desc;
		public Button Kind, Del;
		public int Index = -1;
	}

	public string Key_ { get; set; } = "Key";
	public string Value_ { get; set; } = "Value";
	public string Desc_ { get; set; } = "Description";
	public string KeyHint { get; set; } = "Key";
	public string ValueHint { get; set; } = "Value";
	/// <summary>Une colonne Description.</summary>
	public bool ShowDesc { get; set; } = true;
	/// <summary>form-data : une ligne peut etre un fichier.</summary>
	public bool FormMode { get; set; }
	public event EventHandler Changed;

	static readonly Font Bold = new Font ("", 12, FontStyle.Bold);
	readonly List<Row> _pool = new ();
	readonly VScrollBar _bar = new () { Visible = false };
	readonly CodeEditor _bulk = new () { Visible = false, LineNumbers = false };
	List<KV> _rows = new ();
	int _top;
	bool _binding, _laying, _inBulk;

	public KVTable ()
	{
		Controls.Add (_bar);
		Controls.Add (_bulk);
		_bar.Scroll += (s, e) => { _top = _bar.Value; Rebind (); };
		_bulk.TextChanged += (s, e) => { if (!_binding) FromBulk (); };
	}

	/// <summary>Les lignes editees, sur place ; une ligne vide les termine.</summary>
	public void SetRows (List<KV> rows)
	{
		_rows = rows;
		Trail ();
		_top = 0;
		Rebind ();
		if (_inBulk)
			ToBulk ();
	}

	/// <summary>Les lignes changees par le programme.</summary>
	public void Refill ()
	{
		Trail ();
		_top = Math.Min (_top, Math.Max (0, _rows.Count - _pool.Count));
		Rebind ();
	}

	public int Count => _rows.Count (k => k.On && k.Key.Length > 0);

	void Trail ()
	{
		if (_rows.Count == 0 || !_rows[^1].Blank)
			_rows.Add (new KV ());
	}

	int Visible_ => Math.Clamp ((Height - HeadH) / RowH, 1, MaxRows);

	Row AddRow ()
	{
		var r = new Row
		{
			On = new CheckBox { Checked = true },
			Key = new TextBox { PlaceholderText = KeyHint },
			Value = new TextBox { PlaceholderText = ValueHint },
			Desc = new TextBox { Visible = ShowDesc },
			Kind = new Button { Text = "Text", Visible = FormMode, ToolTipText = "A text field, or a file" },
			Del = new Button { Text = "x", ToolTipText = "Remove the row" },
		};
		r.On.CheckedChanged += (s, e) => Edit (r, k => k.On = r.On.Checked);
		r.Key.TextChanged += (s, e) => Edit (r, k => k.Key = r.Key.Text);
		r.Value.TextChanged += (s, e) => Edit (r, k => k.Value = r.Value.Text);
		r.Desc.TextChanged += (s, e) => Edit (r, k => k.Desc = r.Desc.Text);
		r.Del.Click += (s, e) => Remove (r);
		r.Kind.Click += (s, e) => ToggleFile (r);
		_pool.Add (r);
		Controls.AddRange (r.On, r.Key, r.Value, r.Desc, r.Kind, r.Del);
		return r;
	}

	protected override void OnLayout ()
	{
		if (_laying)
			return;
		_laying = true;
		while (_pool.Count < Visible_)
			AddRow ();
		_bulk.SetBounds (0, 0, Width, Height);
		bool bar = _rows.Count > Visible_;
		int w = Width - (bar ? BarW : 0) - 2;
		_bar.SetBounds (Width - BarW, HeadH, BarW, Height - HeadH);
		int check = 26, del = 30, kind = FormMode ? KindW + 4 : 0;
		int cols = w - check - del - kind - 4, key = ShowDesc ? cols * 3 / 10 : cols * 2 / 5, value = ShowDesc ? cols * 2 / 5 : cols - key, desc = cols - key - value;
		for (int i = 0; i < _pool.Count; i++)
		{
			var r = _pool[i];
			int y = HeadH + i * RowH + 2, x = check;
			r.On.SetBounds (4, y, check - 6, 28);
			r.Key.SetBounds (x, y, key - 4, 28);
			x += key;
			if (FormMode)
			{
				r.Kind.SetBounds (x, y, KindW, 28);
				x += KindW + 4;
			}
			r.Value.SetBounds (x, y, value - 4, 28);
			x += value;
			if (ShowDesc)
			{
				r.Desc.SetBounds (x, y, desc - 4, 28);
				x += desc;
			}
			r.Del.SetBounds (x, y, del - 2, 28);
		}
		_laying = false;
		Rebind ();
		Invalidate ();
	}

	void Show_ (Row r, bool on)
	{
		foreach (Control c in new Control[] { r.On, r.Key, r.Value, r.Del })
			if (c.Visible != on)
				c.Visible = on;
		if (r.Desc.Visible != (on && ShowDesc))
			r.Desc.Visible = on && ShowDesc;
		if (r.Kind.Visible != (on && FormMode))
			r.Kind.Visible = on && FormMode;
	}

	void Rebind ()
	{
		_binding = true;
		bool inBulk = _inBulk;
		int shown = inBulk ? 0 : Math.Min (Visible_, _pool.Count);
		_top = Math.Clamp (_top, 0, Math.Max (0, _rows.Count - shown));
		for (int i = 0; i < _pool.Count; i++)
		{
			var r = _pool[i];
			int at = _top + i;
			bool on = i < shown && at < _rows.Count;
			r.Index = on ? at : -1;
			Show_ (r, on);
			if (!on)
				continue;
			var k = _rows[at];
			if (r.On.Checked != k.On)
				r.On.Checked = k.On;
			if (r.Key.Text != k.Key)
				r.Key.Text = k.Key;
			if (r.Value.Text != k.Value)
				r.Value.Text = k.Value;
			if (ShowDesc && r.Desc.Text != k.Desc)
				r.Desc.Text = k.Desc;
			if (FormMode)
				r.Kind.Text = k.File ? "File" : "Text";
		}
		bool bar = !inBulk && _rows.Count > shown;
		_bar.Maximum = Math.Max (0, _rows.Count - 1);
		_bar.LargeChange = Math.Max (1, shown);
		_bar.Value = _top;
		if (_bar.Visible != bar)
			_bar.Visible = bar;
		_binding = false;
	}

	void Edit (Row r, Action<KV> set)
	{
		if (_binding || r.Index < 0 || r.Index >= _rows.Count)
			return;
		set (_rows[r.Index]);
		if (!_rows[^1].Blank)
		{
			_rows.Add (new KV ());
			Rebind ();
		}
		Changed?.Invoke (this, EventArgs.Empty);
	}

	void Remove (Row r)
	{
		if (r.Index < 0 || r.Index >= _rows.Count)
			return;
		_rows.RemoveAt (r.Index);
		Refill ();
		Changed?.Invoke (this, EventArgs.Empty);
	}

	void ToggleFile (Row r)
	{
		if (r.Index < 0 || r.Index >= _rows.Count)
			return;
		var k = _rows[r.Index];
		if (!k.File)
		{
			using var d = new OpenFileDialog { InitialDirectory = "SD0:/" };
			if (d.ShowDialog () != DialogResult.OK)
				return;
			k.Value = d.FileName;
		}
		k.File = !k.File;
		Edit (r, x => { });
		Rebind ();
	}

	protected override void OnMouseWheel (MouseEventArgs e)
	{
		_top -= Math.Sign (e.Delta);
		Rebind ();
	}

	protected override void OnPaint (PaintEventArgs e)
	{
		var g = e.Graphics;
		g.Clear (ThemeColors.Face);
		if (_inBulk)
			return;
		var ink = new SolidBrush (ThemeColors.TextDim);
		int cols = Width - 26 - 30 - (FormMode ? KindW + 4 : 0) - 6, key = ShowDesc ? cols * 3 / 10 : cols * 2 / 5, value = ShowDesc ? cols * 2 / 5 : cols - key;
		g.DrawString (Key_, Bold, ink, 26, 3);
		g.DrawString (Value_, Bold, ink, 26 + key + (FormMode ? KindW + 4 : 0), 3);
		if (ShowDesc)
			g.DrawString (Desc_, Bold, ink, 26 + key + value + (FormMode ? KindW + 4 : 0), 3);
		g.FillRectangle (new SolidBrush (ThemeColors.Line), 0, HeadH - 2, Width, 1);
	}

	/// <summary>Bulk Edit : une ligne par rangee, « cle: valeur » ; // la desactive.</summary>
	public bool Bulk
	{
		get => _inBulk;
		set
		{
			_inBulk = value;
			if (value)
				ToBulk ();
			_bulk.Visible = value;
			Rebind ();
			Invalidate ();
		}
	}

	void ToBulk ()
	{
		var o = new StringBuilder ();
		foreach (var k in _rows.Where (k => !k.Blank))
			o.Append (k.On ? "" : "//").Append (k.Key).Append (": ").Append (k.Value).Append ('\n');
		_binding = true;
		_bulk.Text = o.ToString ();
		_binding = false;
	}

	void FromBulk ()
	{
		var keep = _rows.ToList ();
		_rows.Clear ();
		foreach (var line in _bulk.Text.Split ('\n'))
		{
			string l = line.Trim ();
			if (l.Length == 0)
				continue;
			bool off = l.StartsWith ("//");
			if (off)
				l = l.Substring (2);
			int c = l.IndexOf (':');
			var kv = new KV (c >= 0 ? l.Substring (0, c).Trim () : l, c >= 0 ? l.Substring (c + 1).Trim () : "", !off);
			kv.Desc = keep.FirstOrDefault (k => k.Key == kv.Key)?.Desc ?? "";
			_rows.Add (kv);
		}
		_rows.Add (new KV ());
		Changed?.Invoke (this, EventArgs.Empty);
	}
}
