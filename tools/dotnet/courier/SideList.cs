// SideList.cs -- la liste de la barre laterale, dessinee ici aux couleurs du bureau : les collections en arbre
// (dossiers, requetes et leur methode), les environnements, l'historique par jour. Un clic ouvre, le bouton droit
// ou le « ... » donne le menu de la ligne. Sidebar est le reste : le choix du lieu, le filtre, le bouton +.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

enum RowKind { Coll, Folder, Req, Globals, Env, Day, Hist, Empty }

/// <summary>Les couleurs de Courier, tirees du theme.</summary>
static class Look
{
	public static Color Mix (Color a, Color b, int t) =>
		Color.FromArgb ((a.R * (255 - t) + b.R * t) / 255, (a.G * (255 - t) + b.G * t) / 255, (a.B * (255 - t) + b.B * t) / 255);

	public static Color Method (string m) => m.ToUpperInvariant () switch
	{
		"GET" or "HEAD" => Color.FromArgb (0x2E, 0x9E, 0x5B),
		"POST" => Color.FromArgb (0xC9, 0x8A, 0x00),
		"PUT" => Color.FromArgb (0x2F, 0x7B, 0xD8),
		"PATCH" => Color.FromArgb (0x8E, 0x5B, 0xD0),
		"DELETE" => ThemeColors.Danger,
		_ => Color.FromArgb (0xC0, 0x50, 0x8A),
	};

	public static Color Status (int s) => s >= 500 || s < 0 ? ThemeColors.Danger : s >= 400 ? Color.FromArgb (0xC9, 0x7A, 0x00) : s >= 300 ? Color.FromArgb (0x2F, 0x7B, 0xD8) : Color.FromArgb (0x2E, 0x9E, 0x5B);
}

sealed class SideRow
{
	public RowKind Kind;
	public object Ptr;
	public int Idx, Depth;
}

class SideList : Control
{
	const int RowH = 28;
	static readonly Font Sans = new Font ("", 13), Bold = new Font ("", 13, FontStyle.Bold), Small = new Font ("", 11, FontStyle.Bold);
	public readonly List<SideRow> Rows = new ();
	public MainForm App;
	public int Mode;			// 0 collections, 1 environnements, 2 historique
	public string Filter = "";
	int _top, _hot = -1;
	bool _hotMore;

	int Shown => Math.Max (1, Height / RowH);
	bool Matches (string s) => Filter.Length == 0 || Util.HasI (s, Filter);
	bool AnyMatch (Item it) => Matches (it.Name) || it.Kids.Any (AnyMatch);

	void AddItems (Item parent, int depth)
	{
		bool filt = Filter.Length > 0;
		foreach (bool folders in new[] { true, false })		// (les dossiers d'abord, comme Postman)
			foreach (var it in parent.Kids.Where (k => k.Folder == folders && (!filt || AnyMatch (k))))
			{
				Rows.Add (new SideRow { Kind = it.Folder ? RowKind.Folder : RowKind.Req, Ptr = it, Depth = depth });
				if (it.Folder && (it.Open || filt))
					AddItems (it, depth + 1);
			}
	}

	public void Rebuild ()
	{
		Rows.Clear ();
		var st = App.Store;
		if (Mode == 0)
		{
			for (int i = 0; i < st.Colls.Count; i++)
			{
				var c = st.Colls[i];
				if (Filter.Length > 0 && !Matches (c.Name) && !AnyMatch (c.Root))
					continue;
				Rows.Add (new SideRow { Kind = RowKind.Coll, Ptr = c, Idx = i });
				if (c.Root.Open || Filter.Length > 0)
					AddItems (c.Root, 1);
			}
		}
		else if (Mode == 1)
		{
			if (Matches ("Globals"))
				Rows.Add (new SideRow { Kind = RowKind.Globals, Idx = -1 });
			for (int i = 0; i < st.Envs.Count; i++)
				if (Matches (st.Envs[i].Name))
					Rows.Add (new SideRow { Kind = RowKind.Env, Ptr = st.Envs[i], Idx = i });
		}
		else
		{
			long lastDay = -1, today = Util.Now / 86400;
			for (int i = 0; i < st.History.Count; i++)
			{
				var h = st.History[i];
				if (!Matches (h.Req.Url) && !Matches (h.Req.Method))
					continue;
				long day = h.When / 86400;
				if (day != lastDay)
				{
					Rows.Add (new SideRow { Kind = RowKind.Day, Ptr = h, Idx = (int) day, Depth = (int) (today - day) });
					lastDay = day;
				}
				Rows.Add (new SideRow { Kind = RowKind.Hist, Ptr = h, Idx = i });
			}
		}
		if (Rows.Count == 0)
			Rows.Add (new SideRow { Kind = RowKind.Empty });
		_top = Math.Clamp (_top, 0, Math.Max (0, Rows.Count - Shown));
		Invalidate ();
	}

	bool Current (SideRow r)
	{
		var t = App.Cur;
		if (t == null)
			return false;
		return r.Kind switch
		{
			RowKind.Req => t.Kind == TabKind.Request && t.ItemId == ((Item) r.Ptr).Id,
			RowKind.Coll => t.Kind == TabKind.Coll && t.CollId == ((Collection) r.Ptr).Id,
			RowKind.Env => t.Kind == TabKind.Env && t.EnvId == ((Env) r.Ptr).Id,
			RowKind.Globals => t.Kind == TabKind.Env && t.EnvId == "globals",
			_ => false,
		};
	}

	static string Short (string m) => m.ToUpperInvariant () switch { "DELETE" => "DEL", "OPTIONS" => "OPT", "PATCH" => "PTCH", var s => s };

	static void Triangle (Graphics g, Brush b, int x, int y, bool open)
	{
		var p = open ? new[] { new Point (x, y - 2), new Point (x + 8, y - 2), new Point (x + 4, y + 3) }
			     : new[] { new Point (x + 2, y - 4), new Point (x + 2, y + 4), new Point (x + 7, y) };
		g.FillPolygon (b, p);
	}

	protected override void OnPaint (PaintEventArgs e)
	{
		var g = e.Graphics;
		Color bg = ThemeColors.Face, ink = ThemeColors.Text, dim = ThemeColors.TextDim;
		g.Clear (bg);
		var inkB = new SolidBrush (ink);
		var dimB = new SolidBrush (dim);
		int width = Width - (Rows.Count > Shown ? 6 : 0);
		for (int k = 0; k <= Shown; k++)
		{
			int i = _top + k;
			if (i >= Rows.Count)
				break;
			var r = Rows[i];
			int y = k * RowH, x = 6, ty = y + 6;
			bool hot = i == _hot;
			bool row = r.Kind != RowKind.Day && r.Kind != RowKind.Empty;
			if (row && (Current (r) || hot))
				g.FillRectangle (new SolidBrush (Look.Mix (bg, ThemeColors.Accent, Current (r) ? 70 : 28)), 2, y + 1, width - 4, RowH - 2);
			int more = hot && row ? 24 : 0, max = width - 8 - more;
			switch (r.Kind)
			{
			case RowKind.Coll:
			case RowKind.Folder:
				bool coll = r.Kind == RowKind.Coll;
				var it = coll ? ((Collection) r.Ptr).Root : (Item) r.Ptr;
				int ix = x + r.Depth * 14;
				Triangle (g, dimB, ix, y + RowH / 2, it.Open || Filter.Length > 0);
				g.DrawString (coll ? ((Collection) r.Ptr).Name : it.Name, coll ? Bold : Sans, inkB, ix + 14, ty, max - ix - 14);
				break;
			case RowKind.Req:
				var q = (Item) r.Ptr;
				int rx = x + r.Depth * 14;
				g.DrawString (Short (q.Req.Method), Small, new SolidBrush (Look.Method (q.Req.Method)), rx, ty + 1);
				g.DrawString (q.Name, Sans, inkB, rx + 40, ty, max - rx - 40);
				break;
			case RowKind.Globals:
				g.DrawString ("Globals", Bold, inkB, x + 4, ty, max - x - 4);
				break;
			case RowKind.Env:
				var en = (Env) r.Ptr;
				g.DrawString ((r.Idx == App.Store.ActiveEnv ? "✓ " : "") + en.Name, Sans, inkB, x + 4, ty, max - x - 4);
				break;
			case RowKind.Day:
				string day = r.Depth == 0 ? "Today" : r.Depth == 1 ? "Yesterday" : DateTime.UnixEpoch.AddDays (r.Idx).ToString ("MMMM d, yyyy", System.Globalization.CultureInfo.InvariantCulture);
				g.DrawString (day, Bold, dimB, x + 2, ty + 2);
				break;
			case RowKind.Hist:
				var h = App.Store.History[r.Idx];
				g.DrawString (Short (h.Req.Method), Small, new SolidBrush (Look.Method (h.Req.Method)), x, ty + 1);
				string st = h.Status > 0 ? h.Status.ToString () : h.Status < 0 ? "ERR" : "";
				int sw = st.Length > 0 && more == 0 ? (int) g.MeasureString (st, Sans).Width + 6 : 0;
				string u = h.Req.Url.StartsWith ("https://") ? h.Req.Url.Substring (8) : h.Req.Url.StartsWith ("http://") ? h.Req.Url.Substring (7) : h.Req.Url;
				g.DrawString (u, Sans, inkB, x + 40, ty, max - x - 40 - sw);
				if (sw > 0)
					g.DrawString (st, Sans, new SolidBrush (Look.Status (h.Status)), width - sw, ty);
				break;
			case RowKind.Empty:
				g.DrawString (Mode == 0 ? "No collections yet" : Mode == 1 ? "No environments" : "Nothing sent yet", Bold, inkB, x + 4, ty + 6, max - x);
				g.DrawString (Mode == 0 ? "Save a request (Ctrl+S), or click +." : Mode == 1 ? "Click + to make one." : "The requests sent show here.", Sans, dimB, x + 4, ty + 28, max - x);
				break;
			}
			if (more > 0)
				g.DrawString ("...", Bold, _hotMore ? inkB : dimB, width - 20, ty - 2);
		}
		if (Rows.Count > Shown)			// (ou l'on en est ; la molette defile)
		{
			int h = Math.Max (20, Height * Shown / Rows.Count), y = (Height - h) * _top / Math.Max (1, Rows.Count - Shown);
			g.FillRectangle (new SolidBrush (ThemeColors.Line), Width - 4, y, 3, h);
		}
	}

	protected override void OnMouseMove (MouseEventArgs e)
	{
		int h = _top + e.Y / RowH;
		if (h >= Rows.Count)
			h = -1;
		bool more = h >= 0 && e.X >= Width - 30;
		if (h != _hot || more != _hotMore)
		{
			_hot = h;
			_hotMore = more;
			Invalidate ();
		}
	}

	protected override void OnMouseLeave (EventArgs e)
	{
		_hot = -1;
		Invalidate ();
	}

	protected override void OnMouseWheel (MouseEventArgs e)
	{
		_top = Math.Clamp (_top - Math.Sign (e.Delta) * 2, 0, Math.Max (0, Rows.Count - Shown));
		Invalidate ();
	}

	protected override void OnMouseDown (MouseEventArgs e)
	{
		int i = _top + e.Y / RowH;
		if (i < 0 || i >= Rows.Count)
			return;
		var r = Rows[i];
		if (e.Button == MouseButtons.Right || e.X >= Width - 30)
			Menu (r, e.Location);
		else
			Click_ (r, e.Clicks == 2);
		Invalidate ();
	}

	void Click_ (SideRow r, bool twice)
	{
		switch (r.Kind)
		{
		case RowKind.Coll:
			var c = (Collection) r.Ptr;
			if (twice)
				App.OpenColl (c);
			else
			{
				c.Root.Open = !c.Root.Open;
				Rebuild ();
			}
			break;
		case RowKind.Folder:
			var f = (Item) r.Ptr;
			f.Open = !f.Open;
			Rebuild ();
			break;
		case RowKind.Req: App.OpenItem ((Item) r.Ptr); break;
		case RowKind.Globals: App.OpenEnv (-1); break;
		case RowKind.Env: App.OpenEnv (r.Idx); break;
		case RowKind.Hist: App.OpenHistory (r.Idx); break;
		}
	}

	void Menu (SideRow r, Point at)
	{
		var m = new ContextMenuStrip ();
		switch (r.Kind)
		{
		case RowKind.Coll:
			var c = (Collection) r.Ptr;
			m.Add ("Open (variables, auth)", () => App.OpenColl (c));
			m.Add ("-");
			m.Add ("Add Request", () => App.NewRequest (c.Root));
			m.Add ("Add Folder", () => App.NewFolder (c.Root));
			m.Add ("-");
			m.Add ("Rename...", () => App.RenameColl (c));
			m.Add ("Duplicate", () => App.DuplicateColl (c));
			m.Add ("Export...", () => App.ExportColl (c));
			m.Add ("-");
			m.Add ("Delete...", () => App.DeleteColl (c));
			break;
		case RowKind.Folder:
			var f = (Item) r.Ptr;
			m.Add ("Add Request", () => App.NewRequest (f));
			m.Add ("Add Folder", () => App.NewFolder (f));
			m.Add ("-");
			ItemMenu (m, f);
			break;
		case RowKind.Req:
			var q = (Item) r.Ptr;
			m.Add ("Open", () => App.OpenItem (q));
			m.Add ("-");
			ItemMenu (m, q);
			break;
		case RowKind.Globals:
			m.Add ("Open", () => App.OpenEnv (-1));
			m.Add ("Export...", () => App.ExportEnv (-1));
			break;
		case RowKind.Env:
			int e = r.Idx;
			m.Add ("Open", () => App.OpenEnv (e));
			if (e == App.Store.ActiveEnv)
				m.Add ("Stop Using It", () => App.SetEnv (-1));
			else
				m.Add ("Use It (Set Active)", () => App.SetEnv (e));
			m.Add ("-");
			m.Add ("Rename...", () => App.RenameEnv (e));
			m.Add ("Duplicate", () => App.DuplicateEnv (e));
			m.Add ("Export...", () => App.ExportEnv (e));
			m.Add ("-");
			m.Add ("Delete...", () => App.DeleteEnv (e));
			break;
		case RowKind.Hist:
			int h = r.Idx;
			m.Add ("Open", () => App.OpenHistory (h));
			m.Add ("Delete", () => App.DeleteHistory (h));
			m.Add ("-");
			m.Add ("Clear the History...", App.ClearHistory);
			break;
		default:
			return;
		}
		m.Show (this, at);
	}

	void ItemMenu (ContextMenuStrip m, Item it)
	{
		m.Add ("Rename...", () => App.RenameItem (it));
		m.Add ("Duplicate", () => App.DuplicateItem (it));
		m.Add ("Move Up", () => App.MoveItem (it, -1));
		m.Add ("Move Down", () => App.MoveItem (it, 1));
		m.Add ("-");
		m.Add ("Delete...", () => App.DeleteItem (it));
	}
}

/// <summary>Le statut de la reponse, sa duree, sa taille.</summary>
class StatusChip : Control
{
	static readonly Font Bold = new Font ("", 13, FontStyle.Bold), Sans = new Font ("", 13);
	Response _r;
	public void Show_ (Response r)
	{
		_r = r;
		ToolTipText = r == null ? "" : "Connect " + r.MsConnect + " ms, first byte " + r.MsFirst + " ms, total " + r.MsTotal + " ms; " + r.WireLen + " bytes received (headers " + r.HeadLen + ")"
			+ (r.Redirects > 0 ? ", redirected" : "") + (r.Truncated ? ", TRUNCATED" : "");
		Invalidate ();
	}

	protected override void OnPaint (PaintEventArgs e)
	{
		var g = e.Graphics;
		var bg = ThemeColors.Face;
		g.Clear (bg);
		if (_r == null)
			return;
		string st = _r.Cancelled ? "Cancelled" : _r.Status < 0 ? "Error" : _r.Status + " " + _r.Reason;
		var c = _r.Cancelled ? ThemeColors.TextDim : Look.Status (_r.Status);
		int w = (int) g.MeasureString (st, Bold).Width + 16;
		g.FillRectangle (new SolidBrush (Look.Mix (bg, c, 44)), 4, 2, w, Height - 4);
		g.DrawString (st, Bold, new SolidBrush (c), 12, 4);
		g.DrawString (_r.MsTotal + " ms    " + Util.Size (_r.Body.Length) + (_r.Truncated ? " (truncated)" : ""), Sans, new SolidBrush (ThemeColors.TextDim), w + 16, 4);
	}
}
