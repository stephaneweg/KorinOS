// Forms.cs -- System.Windows.Forms d'ARMPI : l'API de WinForms, chaque controle etant un widget d'UIKit,
// donc au theme du bureau (docs/DOTNET.md). Ce n'est ni le WinForms de Microsoft (Win32) ni celui de Mono
// (qui dessine lui-meme) : on n'ecrit que ce qui sert, au fil des programmes.
#nullable disable
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Kits;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.UIKit;
using System.Windows.Forms.Layout;

[assembly: AssemblyVersion ("10.0.0.0")]

namespace System.Windows.Forms;

public enum DockStyle { None, Top, Bottom, Left, Right, Fill }
[Flags] public enum AnchorStyles { None = 0, Top = 1, Bottom = 2, Left = 4, Right = 8 }
public enum DialogResult { None, OK, Cancel, Abort, Retry, Ignore, Yes, No }
public enum MessageBoxButtons { OK, OKCancel, AbortRetryIgnore, YesNoCancel, YesNo, RetryCancel }
public enum MessageBoxIcon { None, Information = 64, Warning = 48, Error = 16, Question = 32 }
public enum FormWindowState { Normal, Minimized, Maximized }
public enum FormBorderStyle { None, FixedSingle, Fixed3D, FixedDialog, Sizable }
public enum ComboBoxStyle { Simple, DropDown, DropDownList }
public enum View { LargeIcon, Details, SmallIcon, List, Tile }
[Flags] public enum MouseButtons { None = 0, Left = 0x100000, Right = 0x200000, Middle = 0x400000 }
[Flags] public enum Keys
{
	None = 0, Back = 8, Tab = 9, Enter = 13, Escape = 27, Space = 32, PageUp = 33, PageDown = 34, End = 35, Home = 36,
	Left = 37, Up = 38, Right = 39, Down = 40, Insert = 45, Delete = 46,
	D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
	A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	F1 = 112, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
	KeyCode = 0xFFFF, Shift = 0x10000, Control = 0x20000, Alt = 0x40000
}

public class MouseEventArgs : EventArgs
{
	public MouseButtons Button { get; }
	public int Clicks { get; }
	public int X { get; }
	public int Y { get; }
	public int Delta { get; }
	public Point Location => new Point (X, Y);
	public MouseEventArgs (MouseButtons button, int clicks, int x, int y, int delta)
	{
		Button = button; Clicks = clicks; X = x; Y = y; Delta = delta;
	}
}
public delegate void MouseEventHandler (object sender, MouseEventArgs e);

public class KeyEventArgs : EventArgs
{
	public Keys KeyData { get; }
	public Keys KeyCode => KeyData & Keys.KeyCode;
	public bool Shift => (KeyData & Keys.Shift) != 0;
	public bool Control => (KeyData & Keys.Control) != 0;
	public bool Alt => (KeyData & Keys.Alt) != 0;
	public bool Handled { get; set; }
	public KeyEventArgs (Keys keyData) { KeyData = keyData; }
}
public delegate void KeyEventHandler (object sender, KeyEventArgs e);

public class KeyPressEventArgs : EventArgs
{
	public char KeyChar { get; set; }
	public bool Handled { get; set; }
	public KeyPressEventArgs (char keyChar) { KeyChar = keyChar; }
}
public delegate void KeyPressEventHandler (object sender, KeyPressEventArgs e);

public class PaintEventArgs : EventArgs
{
	public Graphics Graphics { get; }
	public Rectangle ClipRectangle { get; }
	public PaintEventArgs (Graphics graphics, Rectangle clip) { Graphics = graphics; ClipRectangle = clip; }
}
public delegate void PaintEventHandler (object sender, PaintEventArgs e);

/// <summary>Les couleurs du bureau, pour dessiner au theme dans OnPaint.</summary>
public static class ThemeColors
{
	static Color Token (long t) => Color.FromArgb (unchecked ((int) (0xFF000000 | Ui.Token (t))));
	public static Color Face => Token (UI.TJ_FACE);
	public static Color Surface => Token (UI.TJ_SURFACE);
	public static Color Line => Token (UI.TJ_LIGNE);
	public static Color Text => Token (UI.TJ_TEXTE1);
	public static Color TextDim => Token (UI.TJ_TEXTE2);
	public static Color Accent => Token (UI.TJ_ACCENT);
	public static Color Danger => Token (UI.TJ_DANGER);
}

/// <summary>Les mesures des widgets d'UIKit (uk_mt_tab, selon l'echelle du theme).</summary>
internal static class Metrics
{
	static long Scale => Math.Min (UI.ui_echelle (), 2);
	internal static int ControlHeight => Scale == 0 ? 30 : Scale == 1 ? 28 : 26;
	internal static int LineHeight => Scale == 0 ? 28 : Scale == 1 ? 26 : 24;
	internal static int TextWidth (string s) => Font.Default.Width (s);
}

public sealed class Cursor
{
	internal readonly long Shape;
	internal Cursor (long shape) { Shape = shape; }
}

public static class Cursors
{
	public static readonly Cursor Default = new (UI.UC_ARROW), Arrow = Default, IBeam = new (UI.UC_TEXT), Hand = new (UI.UC_HAND),
		SizeWE = new (UI.UC_HRESIZE), SizeNS = new (UI.UC_VRESIZE), WaitCursor = new (UI.UC_WAIT), Cross = new (UI.UC_CROSS),
		SizeAll = new (UI.UC_MOVE), SizeNWSE = new (UI.UC_NWSE), SizeNESW = new (UI.UC_NESW), No = new (UI.UC_NO);
}

public class Control : Component
{
	public class ControlCollection : ArrangedElementCollection
	{
		readonly Control _owner;
		public ControlCollection (Control owner) { _owner = owner; }
		public virtual void Add (Control c)
		{
			c.Parent = _owner;
			Items.Add (c);
			if (_owner.Created)
			{
				_owner.LayoutChildren ();	// (avant : il nait a sa taille)
				c.CreateTree ();
			}
		}
		public virtual void AddRange (params Control[] all) { foreach (var c in all) Add (c); }
		public virtual void Remove (Control c)
		{
			if (Items.Remove (c))
				c.Destroy ();
			Application.Redraw ();
		}
		public virtual void Clear ()
		{
			foreach (var c in Items.ToArray ())
				c.Destroy ();
			Items.Clear ();
			Application.Redraw ();
		}
		public virtual Control this[int index] => Items[index];
		public int IndexOf (Control c) => Items.IndexOf (c);
		public bool Contains (Control c) => Items.Contains (c);
	}

	internal long Handle;
	internal bool Created => Handle != 0;
	internal bool IsView;
	bool _hover;
	MouseButtons _pressed;
	Rectangle _bounds;
	string _text = "";
	bool _visible = true, _enabled = true;
	Rectangle _anchorFrom;
	Size _parentFrom;

	public Control () { Controls = new ControlCollection (this); }

	public string Name { get; set; } = "";
	public object Tag { get; set; }
	public Control Parent { get; internal set; }
	public ControlCollection Controls { get; }
	public DockStyle Dock { get; set; }
	/// <summary>Le vide autour d'un controle docke (a Windows, Dock l'ignore).</summary>
	public Padding Margin { get; set; }
	string _tip;
	/// <summary>L'infobulle du controle.</summary>
	public string ToolTipText
	{
		get => _tip;
		set { _tip = value; if (Created) Ui.With (value ?? "", p => UI.ui_w_tip (Handle, p)); }
	}
	public AnchorStyles Anchor { get; set; } = AnchorStyles.Top | AnchorStyles.Left;
	public int TabIndex { get; set; }
	Cursor _cursor;
	/// <summary>La forme du curseur au-dessus du controle.</summary>
	public Cursor Cursor
	{
		get => _cursor ?? Cursors.Default;
		set { _cursor = value; if (Created && value != null) UI.ui_w_cursor (Handle, value.Shape); }
	}
	public bool TabStop { get; set; } = true;
	Font _font;
	public virtual Font Font { get => _font ?? Font.Default; set => _font = value; }
	public Color BackColor { get; set; }	// (les couleurs viennent du theme du bureau)
	public Color ForeColor { get; set; }

	public event EventHandler Click;
	public event EventHandler TextChanged;
	public event EventHandler Resize;
	public event EventHandler DoubleClick;
	public event EventHandler MouseEnter;
	public event EventHandler MouseLeave;
	public event MouseEventHandler MouseDown;
	public event MouseEventHandler MouseUp;
	public event MouseEventHandler MouseMove;
	public event MouseEventHandler MouseWheel;
	public event KeyEventHandler KeyDown;
	public event KeyPressEventHandler KeyPress;
	public event PaintEventHandler Paint;
	public event EventHandler VisibleChanged;
	public event EventHandler HandleCreated;
	public event EventHandler HandleDestroyed;

	public virtual string Text
	{
		get => Created ? ReadText () : _text;
		set
		{
			_text = value ?? "";
			if (Created)
				WriteText (_text);
			if (AutoSize && Ui.Ready)
				ApplyAutoSize ();
		}
	}

	/// <summary>Le controle prend sa taille preferee, a chaque mise en page.</summary>
	public bool AutoSize { get; set; }
	public Size PreferredSize => GetPreferredSize (Size.Empty);
	public virtual Size GetPreferredSize (Size proposed) => _bounds.Size;
	internal string RawText => _text;

	internal void ApplyAutoSize ()
	{
		if (!AutoSize)
			return;
		var p = GetPreferredSize (Size.Empty);
		_anchorFrom.Size = p;
		if (p != _bounds.Size)
			Size = p;
	}
	protected virtual string ReadText () => _text;
	protected virtual void WriteText (string s) => Ui.With (s, p => UI.ui_w_set_text (Handle, p));

	public Rectangle Bounds { get => _bounds; set { _bounds = value; Move (); } }
	public Point Location { get => _bounds.Location; set { _bounds.Location = value; Move (); } }
	public Size Size { get => _bounds.Size; set { _bounds.Size = value; Move (); } }
	public int Left { get => _bounds.X; set { _bounds.X = value; Move (); } }
	public int Top { get => _bounds.Y; set { _bounds.Y = value; Move (); } }
	public int Width { get => _bounds.Width; set { _bounds.Width = value; Move (); } }
	public int Height { get => _bounds.Height; set { _bounds.Height = value; Move (); } }
	public int Right => _bounds.Right;
	public int Bottom => _bounds.Bottom;
	public virtual Rectangle ClientRectangle => new Rectangle (0, 0, _bounds.Width, _bounds.Height);
	public Size ClientSize { get => ClientRectangle.Size; set => Size = value; }

	public bool Visible
	{
		get => _visible;
		set => SetVisibleCore (value);
	}

	protected virtual void SetVisibleCore (bool value)
	{
		bool was = _visible;
		_visible = value;
		if (Created)
		{
			UI.ui_w_show (Handle, value ? 1 : 0);
			Application.Redraw ();
		}
		if (was != value && Dock != DockStyle.None && Dock != DockStyle.Fill)
			Parent?.LayoutChildren ();	// (un controle docke cache ne prend pas de place)
		VisibleTree ();
	}

	/// <summary>Un parent cache cache aussi ses enfants : ils sont prevenus comme lui.</summary>
	void VisibleTree ()
	{
		OnVisibleChanged (EventArgs.Empty);
		foreach (var c in Controls.Items)
			c.VisibleTree ();
	}

	bool _pageHidden;
	/// <summary>UIKit montre et cache lui-meme les pages d'un TabControl : Visible n'en sait rien.</summary>
	internal void PageHidden (bool hidden)
	{
		if (hidden != _pageHidden)
		{
			_pageHidden = hidden;
			VisibleTree ();
		}
	}

	/// <summary>Vraiment a l'ecran : lui, ses parents, et sa page d'onglet.</summary>
	public bool Showing
	{
		get
		{
			for (var c = this; c != null; c = c.Parent)
				if (!c._visible || c._pageHidden)
					return false;
			return true;
		}
	}

	/// <summary>La place du controle dans le contenu de sa fenetre, pour ui_surface et les autres zones d'UIKit.</summary>
	public Point LocationInForm => Absolute ();
	public bool Enabled
	{
		get => _enabled;
		set { _enabled = value; if (Created) UI.ui_w_enable (Handle, value ? 1 : 0); }
	}

	public void Show () => Visible = true;
	public void Hide () => Visible = false;
	public void Focus () { if (Created) UI.ui_w_focus (Handle); }
	public void Invalidate ()
	{
		if (Created && IsView)
		{
			UI.ui_view_dirty (Handle);
			Application.ViewsDirty ();
		}
		else if (Created)
		{
			UI.ui_w_draw (Handle);
		}
	}
	public void Refresh () => Invalidate ();
	public void SuspendLayout () { }
	public void ResumeLayout () => LayoutChildren ();
	public void ResumeLayout (bool performLayout) { if (performLayout) LayoutChildren (); }
	public void PerformLayout () => LayoutChildren ();
	public void BeginInvoke (Action a) => Application.Post (a);
	public void Invoke (Action a)
	{
		using var done = new System.Threading.ManualResetEventSlim ();
		Application.Post (() => { try { a (); } finally { done.Set (); } });
		done.Wait ();
	}
	public bool InvokeRequired => !Application.OnUiThread;

	/// <summary>Coordonnees dans la fenetre : UIKit ne connait pas de repere par conteneur.</summary>
	internal Point Absolute ()
	{
		var p = _bounds.Location;
		for (var c = Parent; c != null && c is not Form; c = c.Parent)
		{
			p.Offset (c.Left, c.Top);
			var o = c.ClientOffset;
			p.Offset (o.X, o.Y);
		}
		return p;
	}
	internal virtual Point ClientOffset => Point.Empty;

	void Move ()
	{
		if (Created && this is not Form)
		{
			var p = Absolute ();
			UI.ui_w_move (Handle, p.X, p.Y, _bounds.Width, _bounds.Height);
			if (IsView)
				Invalidate ();
		}
		LayoutChildren ();	// (avant la creation aussi : un widget naitra a sa taille)
		if (Created)
			Resize?.Invoke (this, EventArgs.Empty);
	}

	/// <summary>Un Control a soi : une vue d'UIKit, dessinee par OnPaint.</summary>
	internal virtual long Create (int x, int y, int w, int h) => CreateView (x, y, w, h);

	internal long CreateView (int x, int y, int w, int h)
	{
		bool keys = TabStop && (KeyDown != null || KeyPress != null || Overrides ("OnKeyDown") || Overrides ("OnKeyPress"));
		long v = UI.ui_view (x, y, w, h, keys ? UI.UVW_FOCUS : 0);
		if (v != 0)
		{
			IsView = true;
			Application.ViewCallbacks (v);
		}
		return v;
	}

	bool Overrides (string method) =>
		GetType ().GetMethod (method, BindingFlags.Instance | BindingFlags.NonPublic).DeclaringType != typeof (Control);

	/// <summary>Un conteneur est une vue seulement s'il dessine ou ecoute la souris et le clavier.</summary>
	internal bool DrawsItself => Paint != null || MouseDown != null || MouseUp != null || MouseMove != null
		|| MouseWheel != null || KeyDown != null || KeyPress != null || Overrides ("OnPaint") || Overrides ("OnMouseDown")
		|| Overrides ("OnMouseUp") || Overrides ("OnMouseMove") || Overrides ("OnMouseWheel") || Overrides ("OnKeyDown")
		|| Overrides ("OnKeyPress");

	internal virtual long PanelHandle => Handle;
	internal virtual bool Attached => false;	// (UIKit l'a deja rattache a son parent)

	internal virtual void CreateTree ()
	{
		var p = Absolute ();
		Handle = Create (p.X, p.Y, _bounds.Width, _bounds.Height);
		if (Handle == 0)
			throw new InvalidOperationException ("UIKit : widget refuse : " + GetType ().Name);
		Application.Map[Handle] = this;
		if (_cursor != null)
			UI.ui_w_cursor (Handle, _cursor.Shape);
		if (_tip != null)
			Ui.With (_tip, p => UI.ui_w_tip (Handle, p));
		if (!Attached && Parent != null && Parent is not Form && Parent.PanelHandle != 0)
			UI.ui_w_set_parent (Handle, Parent.PanelHandle);
		if (!_visible)
			UI.ui_w_show (Handle, 0);
		if (!_enabled)
			UI.ui_w_enable (Handle, 0);
		_anchorFrom = _bounds;
		_parentFrom = Parent?.ClientRectangle.Size ?? Size.Empty;
		foreach (var c in Controls.Items)
			c.CreateTree ();
		OnHandleCreated (EventArgs.Empty);
	}

	/// <summary>Dock et Anchor, comme WinForms.</summary>
	internal virtual void LayoutChildren ()
	{
		var r = ClientRectangle;
		if (Ui.Ready)
			foreach (var c in Controls.Items)
				c.ApplyAutoSize ();
		foreach (var c in Controls.Items)
		{
			int m = c.Margin.Left;
			if (!c.Visible)
				continue;
			switch (c.Dock)
			{
			case DockStyle.Top: c.SetBounds (r.X + m, r.Y + m, r.Width - 2 * m, c.Height); r.Y += c.Height + 2 * m; r.Height -= c.Height + 2 * m; break;
			case DockStyle.Bottom: r.Height -= c.Height + 2 * m; c.SetBounds (r.X + m, r.Bottom + m, r.Width - 2 * m, c.Height); break;
			case DockStyle.Left: c.SetBounds (r.X + m, r.Y + m, c.Width, r.Height - 2 * m); r.X += c.Width + 2 * m; r.Width -= c.Width + 2 * m; break;
			case DockStyle.Right: r.Width -= c.Width + 2 * m; c.SetBounds (r.Right + m, r.Y + m, c.Width, r.Height - 2 * m); break;
			}
		}
		foreach (var c in Controls.Items)
		{
			if (c.Dock == DockStyle.Fill)
				c.SetBounds (r.X + c.Margin.Left, r.Y + c.Margin.Top, r.Width - c.Margin.Horizontal, r.Height - c.Margin.Vertical);
			else if (c.Dock == DockStyle.None)
				c.ApplyAnchor (ClientRectangle.Size);
		}
		OnLayout ();
	}

	/// <summary>Apres Dock et Anchor : un controle compose y place ses enfants lui-meme.</summary>
	protected virtual void OnLayout () { }

	void ApplyAnchor (Size parent)
	{
		if (_parentFrom.IsEmpty)
			return;
		int dw = parent.Width - _parentFrom.Width, dh = parent.Height - _parentFrom.Height;
		var b = _anchorFrom;
		if ((Anchor & AnchorStyles.Right) != 0)
		{
			if ((Anchor & AnchorStyles.Left) != 0) b.Width += dw; else b.X += dw;
		}
		if ((Anchor & AnchorStyles.Bottom) != 0)
		{
			if ((Anchor & AnchorStyles.Top) != 0) b.Height += dh; else b.Y += dh;
		}
		SetBounds (b.X, b.Y, b.Width, b.Height);
	}

	public void SetBounds (int x, int y, int w, int h) { _bounds = new Rectangle (x, y, w, h); Move (); }

	/// <summary>Un code UN_* d'UIKit.</summary>
	internal virtual void Notify (long code)
	{
		if (code == UI.UN_ACTIVATE)
			OnClick (EventArgs.Empty);
		else if (code == UI.UN_CHANGE)
			OnTextChanged (EventArgs.Empty);
	}
	protected virtual void OnClick (EventArgs e) => Click?.Invoke (this, e);
	protected virtual void OnTextChanged (EventArgs e) => TextChanged?.Invoke (this, e);
	protected virtual void OnDoubleClick (EventArgs e) => DoubleClick?.Invoke (this, e);
	protected virtual void OnMouseEnter (EventArgs e) => MouseEnter?.Invoke (this, e);
	protected virtual void OnMouseLeave (EventArgs e) => MouseLeave?.Invoke (this, e);
	protected virtual void OnMouseDown (MouseEventArgs e) => MouseDown?.Invoke (this, e);
	protected virtual void OnMouseUp (MouseEventArgs e) => MouseUp?.Invoke (this, e);
	protected virtual void OnMouseMove (MouseEventArgs e) => MouseMove?.Invoke (this, e);
	protected virtual void OnMouseWheel (MouseEventArgs e) => MouseWheel?.Invoke (this, e);
	protected virtual void OnKeyDown (KeyEventArgs e) => KeyDown?.Invoke (this, e);
	protected virtual void OnKeyPress (KeyPressEventArgs e) => KeyPress?.Invoke (this, e);
	protected virtual void OnPaint (PaintEventArgs e) => Paint?.Invoke (this, e);
	protected virtual void OnVisibleChanged (EventArgs e) => VisibleChanged?.Invoke (this, e);
	protected virtual void OnHandleCreated (EventArgs e) => HandleCreated?.Invoke (this, e);
	protected virtual void OnHandleDestroyed (EventArgs e) => HandleDestroyed?.Invoke (this, e);

	static MouseButtons Buttons (int b) => (MouseButtons) (((b & 1) != 0 ? 0x100000 : 0) | ((b & 2) != 0 ? 0x200000 : 0) | ((b & 4) != 0 ? 0x400000 : 0));

	internal void ViewPaint (int w, int h) => OnPaint (new PaintEventArgs (new Graphics (w, h), new Rectangle (0, 0, w, h)));

	/// <summary>genre : 0 deplacement, 1 appui, 2 relache, 3 sortie ; infos : clics | boutons << 8 | mods << 16.</summary>
	internal void ViewMouse (int kind, int x, int y, int info)
	{
		var b = Buttons ((info >> 8) & 255);
		int clicks = info & 255;
		if (kind == 3)
		{
			_hover = false;
			OnMouseLeave (EventArgs.Empty);
			return;
		}
		if (!_hover)
		{
			_hover = true;
			OnMouseEnter (EventArgs.Empty);
		}
		if (kind == 0)
		{
			OnMouseMove (new MouseEventArgs (b, 0, x, y, 0));
		}
		else if (kind == 1)
		{
			_pressed = b;
			OnMouseDown (new MouseEventArgs (b, clicks, x, y, 0));
			if (clicks == 2)
				OnDoubleClick (EventArgs.Empty);
		}
		else
		{
			var was = _pressed;
			_pressed = MouseButtons.None;
			OnMouseUp (new MouseEventArgs (was, clicks, x, y, 0));
			if (ClientRectangle.Contains (x, y))
				OnClick (EventArgs.Empty);
		}
	}

	static Keys KeyOf (int hid)
	{
		if (hid >= 4 && hid <= 29) return Keys.A + (hid - 4);
		if (hid >= 30 && hid <= 38) return Keys.D1 + (hid - 30);
		if (hid >= 58 && hid <= 69) return Keys.F1 + (hid - 58);
		return hid switch
		{
			39 => Keys.D0, 40 => Keys.Enter, 41 => Keys.Escape, 42 => Keys.Back, 43 => Keys.Tab, 44 => Keys.Space,
			73 => Keys.Insert, 74 => Keys.Home, 75 => Keys.PageUp, 76 => Keys.Delete, 77 => Keys.End, 78 => Keys.PageDown,
			79 => Keys.Right, 80 => Keys.Left, 81 => Keys.Down, 82 => Keys.Up, _ => Keys.None
		};
	}

	internal static Keys ToKeys (int hid, int mods)
	{
		var k = KeyOf (hid);
		if ((mods & 0x22) != 0) k |= Keys.Shift;
		if ((mods & 0x11) != 0) k |= Keys.Control;
		if ((mods & 0x44) != 0) k |= Keys.Alt;
		return k;
	}

	/// <summary>-> vrai si le programme l'a prise.</summary>
	internal bool ViewKey (int hid, int ch, int mods)
	{
		var e = new KeyEventArgs (ToKeys (hid, mods));
		OnKeyDown (e);
		if (e.Handled || ch == 0)
			return e.Handled;
		var p = new KeyPressEventArgs ((char) ch);
		OnKeyPress (p);
		return p.Handled;
	}

	internal void ViewWheel (int notches) => OnMouseWheel (new MouseEventArgs (MouseButtons.None, 0, 0, 0, notches * 120));

	/// <summary>Le controle et ses enfants disparaissent d'UIKit.</summary>
	internal void Destroy ()
	{
		foreach (var c in Controls.Items)
			c.Destroy ();
		if (Handle != 0)
		{
			OnHandleDestroyed (EventArgs.Empty);
			Application.Map.Remove (Handle);
			UI.ui_w_destroy (Handle);
		}
		Handle = 0;
	}

	internal void DestroyTree ()
	{
		foreach (var c in Controls.Items)
			c.DestroyTree ();
		if (Handle != 0)
		{
			Keep ();
			OnHandleDestroyed (EventArgs.Empty);
			Application.Map.Remove (Handle);
		}
		Handle = 0;
	}

	/// <summary>Le widget va disparaitre : ce qu'on y a saisi se lit encore apres un dialogue.</summary>
	internal virtual void Keep () => _text = ReadText ();
}

public abstract class ButtonBase : Control
{
	public bool UseVisualStyleBackColor { get; set; }
}

public interface IButtonControl
{
	DialogResult DialogResult { get; set; }
	void NotifyDefault (bool value);
	void PerformClick ();
}

public class Button : ButtonBase, IButtonControl
{
	public override Size GetPreferredSize (Size proposed) => new Size (Math.Max (Metrics.TextWidth (Text) + 28, 80), Metrics.ControlHeight);
	public virtual DialogResult DialogResult { get; set; }
	public virtual void NotifyDefault (bool value) { }
	public void PerformClick () => OnClick (EventArgs.Empty);
	public Button () { Size = new Size (75, 28); }
	internal override long Create (int x, int y, int w, int h) => Ui.With (Text, t => UI.ui_pushbutton (x, y, w, h, t, 0));
	protected override void OnClick (EventArgs e)
	{
		base.OnClick (e);
		if (DialogResult != DialogResult.None)
			FindForm ()?.CloseDialog (DialogResult);
	}
	Form FindForm ()
	{
		var c = (Control) this;
		while (c != null && c is not Form)
			c = c.Parent;
		return (Form) c;
	}
}

public class Label : Control
{
	public override Size GetPreferredSize (Size proposed) => new Size (Metrics.TextWidth (Text) + 2, Metrics.LineHeight);
	public Label () { Size = new Size (100, 24); }
	internal override long Create (int x, int y, int w, int h) => Ui.With (Text, t => UI.ui_label (x, y, w, h, t, 0));
}

public class TextBox : Control
{
	public override Size GetPreferredSize (Size proposed) => new Size (Width > 0 ? Width : 120, Multiline ? Height : Metrics.ControlHeight);
	public bool Multiline { get; set; }
	public bool ReadOnly { get; set; }
	public char PasswordChar { get; set; }
	public bool UseSystemPasswordChar { get; set; }
	string _hint;
	/// <summary>Le texte gris d'un champ vide.</summary>
	public string PlaceholderText
	{
		get => _hint;
		set { _hint = value; if (Created && !Multiline) Ui.With (value ?? "", p => UI.ui_textbox_hint (Handle, p)); }
	}
	public TextBox () { Size = new Size (120, 28); }
	internal override long Create (int x, int y, int w, int h)
	{
		long s = ReadOnly ? UI.UTF_READONLY : 0;
		if (Multiline)
			return Finish (UI.ui_textarea (x, y, w, h, s | UI.UTA_VSCROLL));
		if (PasswordChar != 0 || UseSystemPasswordChar)
			s |= UI.UTF_PASSWORD;
		return Finish (UI.ui_textbox (x, y, w, h, s));
	}
	long Finish (long h)
	{
		var t = RawText;
		if (h != 0 && t.Length > 0)
			Ui.With (t, p => UI.ui_w_set_text (h, p));
		if (h != 0 && _hint != null && !Multiline)
			Ui.With (_hint, p => UI.ui_textbox_hint (h, p));
		return h;
	}
	protected override string ReadText () => KitLoader.String (UI.ui_w_get_text (Handle));
}

public class CheckBox : ButtonBase
{
	public override Size GetPreferredSize (Size proposed) => new Size (Metrics.TextWidth (Text) + 26, Metrics.ControlHeight);
	bool _checked;
	public CheckBox () { Size = new Size (120, 24); }
	public event EventHandler CheckedChanged;
	public bool Checked
	{
		get => Created ? UI.ui_w_get_value (Handle) != 0 : _checked;
		set { _checked = value; if (Created) UI.ui_w_set_value (Handle, value ? 1 : 0); }
	}
	internal override long Create (int x, int y, int w, int h) => Ui.With (Text, t => UI.ui_checkbox (x, y, w, h, t, _checked ? 1 : 0));
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			CheckedChanged?.Invoke (this, EventArgs.Empty);
	}
	protected override string ReadText () => base.ReadText ();
}

public class RadioButton : ButtonBase
{
	public override Size GetPreferredSize (Size proposed) => new Size (Metrics.TextWidth (Text) + 26, Metrics.ControlHeight);
	public RadioButton () { Size = new Size (120, 24); }
	public event EventHandler CheckedChanged;
	public bool Checked
	{
		get => Created && UI.ui_w_get_value (Handle) != 0;
		set { if (Created) UI.ui_w_set_value (Handle, value ? 1 : 0); }
	}
	internal override long Create (int x, int y, int w, int h)
	{
		long group = RuntimeHelpers.GetHashCode (Parent) & 0xFFFF;	// (les radios d'un conteneur s'excluent)
		return Ui.With (Text, t => UI.ui_radio (x, y, w, h, t, group));
	}
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			CheckedChanged?.Invoke (this, EventArgs.Empty);
	}
}

/// <summary>Elements d'une liste : les changements suivent dans le widget.</summary>
public class ItemCollection : List<object>
{
	readonly Control _owner;
	internal ItemCollection (Control owner) { _owner = owner; }
	public new void Add (object o)
	{
		base.Add (o);
		if (_owner.Created)
			Ui.With (o?.ToString (), p => UI.ui_list_add (_owner.Handle, p, 0));
		Application.Redraw ();
	}
	public void AddRange (params object[] all) { foreach (var o in all) Add (o); }
	public new void Clear ()
	{
		base.Clear ();
		if (_owner.Created)
			UI.ui_list_clear (_owner.Handle);
	}
	public new void RemoveAt (int i)
	{
		base.RemoveAt (i);
		if (_owner.Created)
			UI.ui_list_remove (_owner.Handle, i);
	}
}

public abstract class ListControl : Control
{
	public ItemCollection Items { get; }
	protected ListControl () { Items = NewItems (); }
	internal virtual ItemCollection NewItems () => new ItemCollection (this);
	public bool FormattingEnabled { get; set; }
	public event EventHandler SelectedIndexChanged;
	int _selected = -1;
	public int SelectedIndex
	{
		get => Created ? (int) UI.ui_w_get_value (Handle) : _selected;
		set { _selected = value; if (Created) UI.ui_w_set_value (Handle, value); }
	}
	internal override void CreateTree ()
	{
		base.CreateTree ();
		if (_selected >= 0)
			UI.ui_w_set_value (Handle, _selected);
	}
	internal override void Keep ()
	{
		base.Keep ();
		_selected = (int) UI.ui_w_get_value (Handle);
	}
	public object SelectedItem => SelectedIndex >= 0 && SelectedIndex < Items.Count ? Items[SelectedIndex] : null;
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			SelectedIndexChanged?.Invoke (this, EventArgs.Empty);
	}
	protected void FillItems (long h)
	{
		foreach (var o in Items)
			Ui.With (o?.ToString (), p => UI.ui_list_add (h, p, 0));
	}
}

public class ListBox : ListControl
{
	public class ObjectCollection : ItemCollection
	{
		public ObjectCollection (ListBox owner) : base (owner) { }
		public new int Add (object o) { base.Add (o); return Count - 1; }
		public new void AddRange (params object[] all) => base.AddRange (all);
		public new int Count => base.Count;
	}
	internal override ItemCollection NewItems () => new ObjectCollection (this);
	public new ObjectCollection Items => (ObjectCollection) base.Items;
	public ListBox () { Size = new Size (120, 96); }
	internal override long Create (int x, int y, int w, int h)
	{
		long l = UI.ui_list (x, y, w, h, 0);
		if (l != 0)
		{
			Ui.With ("", t => UI.ui_list_column (l, t, w - 16, 0));
			FillItems (l);
		}
		return l;
	}
}

public class ComboBox : ListControl
{
	public ComboBoxStyle DropDownStyle { get; set; } = ComboBoxStyle.DropDown;
	public ComboBox () { Size = new Size (120, 28); }
	internal override long Create (int x, int y, int w, int h)
	{
		string all = string.Join ("|", Items);
		long c = DropDownStyle == ComboBoxStyle.DropDownList
			? Ui.With (all, t => UI.ui_dropdown (x, y, w, h, t))
			: Ui.With (all, t => UI.ui_combobox (x, y, w, h, t));
		return c;
	}
	protected override string ReadText () => DropDownStyle == ComboBoxStyle.DropDownList ? SelectedItem?.ToString () ?? "" : KitLoader.String (UI.ui_w_get_text (Handle));
}

public class ColumnHeader
{
	public string Text { get; set; } = "";
	public int Width { get; set; } = 100;
}

public class ListViewItem
{
	public string Text { get; set; }
	public List<string> SubItems { get; } = new ();
	public object Tag { get; set; }
	public ListViewItem (string text) { Text = text; }
	public ListViewItem (string[] items)
	{
		Text = items.Length > 0 ? items[0] : "";
		for (int i = 1; i < items.Length; i++)
			SubItems.Add (items[i]);
	}
	public override string ToString () => SubItems.Count == 0 ? Text : Text + "\t" + string.Join ("\t", SubItems);
}

/// <summary>ListView en vue Details : les grandes icones ne sont pas dans UIKit.</summary>
public class ListView : ListControl
{
	public List<ColumnHeader> Columns { get; } = new ();
	public View View { get; set; } = View.Details;
	public bool GridLines { get; set; }
	public bool FullRowSelect { get; set; }
	public ListView () { Size = new Size (200, 120); }
	internal override long Create (int x, int y, int w, int h)
	{
		long l = UI.ui_list (x, y, w, h, UI.UL_HEADER | (GridLines ? UI.UL_GRID : 0));
		if (l != 0)
		{
			foreach (var c in Columns)
				Ui.With (c.Text, t => UI.ui_list_column (l, t, c.Width, 0));
			FillItems (l);
		}
		return l;
	}
}

public class NumericUpDown : Control
{
	decimal _value, _min, _max = 100;
	public NumericUpDown () { Size = new Size (100, 28); }
	public event EventHandler ValueChanged;
	public decimal Minimum { get => _min; set => _min = value; }
	public decimal Maximum { get => _max; set => _max = value; }
	public decimal Value
	{
		get => Created ? UI.ui_w_get_value (Handle) : _value;
		set { _value = value; if (Created) UI.ui_w_set_value (Handle, (long) value); }
	}
	internal override long Create (int x, int y, int w, int h) => UI.ui_numeric (x, y, w, h, (long) _min, (long) _max, (long) _value);
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			ValueChanged?.Invoke (this, EventArgs.Empty);
	}
}

public class TrackBar : Control
{
	int _value, _min, _max = 10;
	public TrackBar () { Size = new Size (120, 28); }
	public event EventHandler ValueChanged;
	public int Minimum { get => _min; set => _min = value; }
	public int Maximum { get => _max; set => _max = value; }
	public int Value
	{
		get => Created ? (int) UI.ui_w_get_value (Handle) : _value;
		set { _value = value; if (Created) UI.ui_w_set_value (Handle, value); }
	}
	internal override long Create (int x, int y, int w, int h) => UI.ui_slider (x, y, w, h, _min, _max, _value);
	internal override void Notify (long code)
	{
		base.Notify (code);
		if (code == UI.UN_CHANGE)
			ValueChanged?.Invoke (this, EventArgs.Empty);
	}
}

public class ProgressBar : Control
{
	int _value, _max = 100;
	public ProgressBar () { Size = new Size (120, 20); }
	public int Maximum { get => _max; set => _max = value; }
	public int Value
	{
		get => _value;
		set { _value = value; if (Created) UI.ui_w_set_value (Handle, value); }
	}
	internal override long Create (int x, int y, int w, int h) => UI.ui_progress (x, y, w, h, _max, _value);
}

public class ScrollableControl : Control
{
	internal override long Create (int x, int y, int w, int h) => DrawsItself ? CreateView (x, y, w, h) : UI.ui_panel (x, y, w, h, 0);
	public override Size GetPreferredSize (Size proposed)
	{
		int w = 0, h = 0;
		foreach (var c in Controls.Items)
		{
			var p = c.AutoSize ? c.PreferredSize : c.Size;
			w = Math.Max (w, c.Left + p.Width);
			h = Math.Max (h, c.Top + p.Height);
		}
		return new Size (w, h);
	}
}

public class Panel : ScrollableControl { }

public enum AutoScaleMode { None, Font, Dpi, Inherit }

public class ContainerControl : ScrollableControl
{
	public SizeF AutoScaleDimensions { get; set; }	// (pas de mise a l'echelle : les pixels du Designer valent ceux d'ici)
	public AutoScaleMode AutoScaleMode { get; set; }
}

/// <summary>Empile ses controles, en colonne ou en ligne : pour les .ui, sans coordonnees.</summary>
public class StackPanel : Panel
{
	public bool Horizontal { get; set; }
	public int Spacing { get; set; } = 6;
	public int Padding { get; set; }

	public override Size GetPreferredSize (Size proposed)
	{
		int along = 2 * Padding, across = 0, n = 0;
		foreach (var c in Controls.Items)
		{
			if (!c.Visible)
				continue;
			var p = c.AutoSize ? c.PreferredSize : c.Size;
			along += Horizontal ? p.Width : p.Height;
			across = Math.Max (across, Horizontal ? p.Height : p.Width);
			n++;
		}
		along += Math.Max (n - 1, 0) * Spacing;
		across += 2 * Padding;
		return Horizontal ? new Size (along, across) : new Size (across, along);
	}

	internal override void LayoutChildren ()
	{
		int at = Padding, across = (Horizontal ? Height : Width) - 2 * Padding;
		foreach (var c in Controls.Items)
		{
			if (!c.Visible)
				continue;
			if (Ui.Ready)
				c.ApplyAutoSize ();
			if (Horizontal)
			{
				c.SetBounds (at, Padding, c.Width, across);
				at += c.Width + Spacing;
			}
			else
			{
				c.SetBounds (Padding, at, across, c.Height);
				at += c.Height + Spacing;
			}
		}
	}
}

public class UserControl : ContainerControl { }

public class GroupBox : Control
{
	internal override long Create (int x, int y, int w, int h) => Ui.With (Text, t => UI.ui_groupbox (x, y, w, h, t));
}

public class PictureBox : Control
{
	long _image;
	public string ImageLocation
	{
		set
		{
			_image = Ui.With (value, p => UI.ui_image_load (p));
			if (Created)
				UI.ui_imagebox_set (Handle, _image);
		}
	}
	/// <summary>L'image montree ; null : aucune.</summary>
	public Image Image
	{
		set
		{
			_image = value?.Handle ?? 0;
			if (Created)
				UI.ui_imagebox_set (Handle, _image);
		}
	}
	internal override long Create (int x, int y, int w, int h) => UI.ui_imagebox (x, y, w, h, _image, 0);
}

public class Timer : Component
{
	long _next;
	bool _on;
	public Timer () { }
	public Timer (IContainer container) { container.Add (this); }
	public int Interval { get; set; } = 100;
	public virtual bool Enabled
	{
		get => _on;
		set
		{
			_on = value;
			Application.Timers.Remove (this);
			if (value)
			{
				_next = Environment.TickCount64 + Interval;
				Application.Timers.Add (this);
			}
		}
	}
	public event EventHandler Tick;
	public void Start () => Enabled = true;
	public void Stop () => Enabled = false;
	protected override void Dispose (bool disposing) { Stop (); base.Dispose (disposing); }
	internal long Due (long now)
	{
		if (now >= _next)
		{
			_next = now + Interval;
			Tick?.Invoke (this, EventArgs.Empty);
		}
		return _next - now;
	}
}

public class FormClosingEventArgs : CancelEventArgs { }
public delegate void FormClosingEventHandler (object sender, FormClosingEventArgs e);

/// <summary>Les entrees d'un menu ; un trait est une entree dont le texte est « - ».</summary>
public class ToolStripItemCollection : List<ToolStripMenuItem>
{
	public ToolStripMenuItem Add (string text)
	{
		var m = new ToolStripMenuItem (text);
		Add (m);
		return m;
	}
	public void Add (ToolStripSeparator separator) => Add ("-");
}

public class ToolStripMenuItem
{
	static readonly Dictionary<int, ToolStripMenuItem> s_ids = new ();
	static int s_next = 1;
	internal int Id = s_next++;
	internal MenuStrip Strip;
	string _text, _display;
	Keys _keys;
	bool _enabled = true, _checked, _visible = true;
	public string Name { get; set; } = "";
	public object Tag { get; set; }
	public ToolStripItemCollection DropDownItems { get; } = new ();
	public bool CheckOnClick { get; set; }
	public event EventHandler Click;
	public event EventHandler CheckedChanged;
	public ToolStripMenuItem (string text = "") { _text = text; s_ids[Id] = this; }
	public ToolStripMenuItem (string text, object image, EventHandler onClick) : this (text) { Click += onClick; }
	public ToolStripMenuItem (string text, object image, EventHandler onClick, Keys shortcutKeys) : this (text, image, onClick) { _keys = shortcutKeys; }

	public string Text { get => _text; set { _text = value; Strip?.Rebuild (); } }
	/// <summary>Une entree cachee n'est pas envoyee a la barre de menus.</summary>
	public bool Visible { get => _visible; set { _visible = value; Strip?.Rebuild (); } }
	public bool Enabled { get => _enabled; set { _enabled = value; Strip?.SendState (this); } }
	public bool Checked
	{
		get => _checked;
		set
		{
			if (_checked == value)
				return;
			_checked = value;
			Strip?.SendState (this);
			CheckedChanged?.Invoke (this, EventArgs.Empty);
		}
	}
	public Keys ShortcutKeys { get => _keys; set { _keys = value; Strip?.Rebuild (); } }
	/// <summary>Le raccourci tel que le comprend UIKit (« Ctrl+Maj+S », « F5 ») ; le texte de ShortcutKeys
	/// si rien n'est donne. Une lettre, un chiffre ou F1 a F12 seulement.</summary>
	public string ShortcutKeyDisplayString { get => _display ?? KeysText (_keys); set { _display = value; Strip?.Rebuild (); } }

	internal long State => (_enabled ? 0 : UI.UMS_DISABLED) | (_checked ? UI.UMS_CHECKED : 0);

	static string KeysText (Keys k)
	{
		Keys code = k & Keys.KeyCode;
		if (code == Keys.None)
			return "";
		string key = code >= Keys.D0 && code <= Keys.D9 ? ((int) code - (int) Keys.D0).ToString () : code.ToString ();
		return ((k & Keys.Control) != 0 ? "Ctrl+" : "") + ((k & Keys.Shift) != 0 ? "Maj+" : "") + ((k & Keys.Alt) != 0 ? "Alt+" : "") + key;
	}

	internal static void Fire (int id)
	{
		if (!s_ids.TryGetValue (id, out var m) || !m._enabled)
			return;
		if (m.CheckOnClick)
			m.Checked = !m._checked;
		m.Click?.Invoke (m, EventArgs.Empty);
	}
}

/// <summary>Le menu de la fenetre : la barre de menus globale du bureau, pas une bande dans la fenetre.</summary>
public class MenuStrip
{
	public ToolStripItemCollection Items { get; } = new ();
	internal Form Owner;
	long _text;

	string Build ()
	{
		var b = new System.Text.StringBuilder ();
		foreach (var m in Items)
		{
			m.Strip = this;
			if (!m.Visible)
				continue;
			b.Append ('M').Append (m.Text).Append ('\n');
			foreach (var e in m.DropDownItems)
			{
				e.Strip = this;
				if (e.Text == "-")
					b.Append ("-\n");
				else if (e.Visible)
					b.Append ('I').Append (e.Id).Append ('\t').Append (e.Text).Append ('\t').Append (e.ShortcutKeyDisplayString).Append ('\n');
			}
		}
		return b.ToString ();
	}

	bool Live => Owner != null && Owner.Created;

	/// <summary>Le texte des menus et l'etat de leurs entrees, a la barre de menus.</summary>
	internal void Rebuild ()
	{
		if (!Live)
			return;
		long text = KitLoader.Utf8 (Build ());	// (UIKit le garde, et le relit a chaque etat)
		Application.InWindow (Owner.Handle, () =>
		{
			UI.ui_menu (text);
			if (_text != 0)
				KitLoader.Free (_text);
			_text = text;
			foreach (var m in Items)
				foreach (var e in m.DropDownItems)
					if (e.State != 0)
						UI.ui_menu_state (e.Id, e.State);
		});
	}

	internal void SendState (ToolStripMenuItem item)
	{
		if (Live)
			Application.InWindow (Owner.Handle, () => UI.ui_menu_state (item.Id, item.State));
	}
}

public unsafe class Form : ContainerControl
{
	bool _closed;
	long _dialog;
	public FormBorderStyle FormBorderStyle { get; set; } = FormBorderStyle.Sizable;
	public FormWindowState WindowState { get; set; }
	MenuStrip _menu;
	public MenuStrip MainMenuStrip
	{
		get => _menu;
		set
		{
			_menu = value;
			if (value != null)
				value.Owner = this;
			if (Created)
			{
				if (value != null)
					value.Rebuild ();
				else
					Application.InWindow (Handle, () => UI.ui_menu (0));
			}
		}
	}
	public DialogResult DialogResult { get; set; }
	public IButtonControl AcceptButton { get; set; }
	public IButtonControl CancelButton { get; set; }
	/// <summary>La fenetre voit les touches avant ses controles (KeyDown).</summary>
	public bool KeyPreview { get; set; }
	public event EventHandler ClientSizeChanged;
	public event EventHandler Load;
	public event EventHandler Shown;
	public event FormClosingEventHandler FormClosing;
	public event EventHandler FormClosed;

	public Form () { Size = new Size (640, 480); }

	public override string Text
	{
		get => base.Text;
		set { base.Text = value; }
	}
	protected override string ReadText () => TitleText;
	string TitleText = "";
	protected override void WriteText (string s)
	{
		TitleText = s;
		Application.InWindow (Handle, () => Ui.With (s, p => UI.ui_title (p)));
	}
	internal override Point ClientOffset => Point.Empty;

	internal override long Create (int x, int y, int w, int h) => 0;

	internal void Open ()
	{
		Ui.Init ();
		TitleText = TextSeed ();
		long role = FormBorderStyle == FormBorderStyle.Sizable ? UI.WF_RESIZABLE : 0;
		Handle = Ui.With (TitleText, t => UI.ui_window_ex (-1, -1, Width, Height, t, role));
		if (Handle < 0)
			throw new InvalidOperationException ("Elegant refuse la fenetre");
		_closed = false;
		Application.Forms.Add (this);
		Start ();
	}

	/// <summary>Non modal : une fenetre de plus, servie par la boucle d'Application.Run. Elegant ne cache pas une
	/// fenetre : on la ferme, ou on la garde. (Control.Show et Control.Hide passent par ici : un binaire compile les
	/// appelle sur Control, pas sur Form.)</summary>
	protected override void SetVisibleCore (bool value)
	{
		if (!value)
			throw new NotSupportedException ("Form.Hide : utiliser Close");
		if (!Created)
			Open ();
	}

	string TextSeed () { string t = base.Text; return t; }

	void Start ()
	{
		delegate* unmanaged<long, long, void> fn = &Application.Resized;
		UI.ui_on_resize ((long) fn);
		Application.Fill ();
		Application.Map[Handle] = this;
		LayoutChildren ();
		foreach (var c in Controls.Items)
			c.CreateTree ();
		MainMenuStrip?.Rebuild ();
		Load?.Invoke (this, EventArgs.Empty);
		UI.ui_w_draw (0);
		UI.ui_view_flush ();
		UI.ui_update_all ();
		if (WindowState == FormWindowState.Maximized)
			UI.ui_maximize ();
		Shown?.Invoke (this, EventArgs.Empty);
		Application.Redraw ();
	}

	void OnClientResized () => ClientSizeChanged?.Invoke (this, EventArgs.Empty);

	internal void ClientResized (long w, long h)
	{
		SetSizeOnly ((int) w, (int) h);
		Application.Fill ();
		LayoutChildren ();
		OnClientResized ();
		UI.ui_w_draw (0);
		UI.ui_update_all ();
	}

	void SetSizeOnly (int w, int h)
	{
		var b = Bounds;
		long keep = Handle;
		Handle = 0;
		Bounds = new Rectangle (b.X, b.Y, w, h);
		Handle = keep;
	}

	internal bool PreviewKey ()
	{
		var e = new KeyEventArgs (ToKeys (Ui.Event (UI.EV_CODE), Ui.Event (24)));
		OnKeyDown (e);
		return e.Handled;
	}

	public void Close ()
	{
		var e = new FormClosingEventArgs ();
		FormClosing?.Invoke (this, e);
		if (e.Cancel)
			return;
		if (_dialog != 0)
		{
			UI.ui_dialog_close (_dialog, (long) DialogResult);
			return;
		}
		_closed = true;
		FormClosed?.Invoke (this, EventArgs.Empty);
		Application.Closed (this);
	}
	internal bool Closed => _closed;

	internal void CloseDialog (DialogResult r)
	{
		DialogResult = r;
		Close ();
	}

	/// <summary>Modal : une fenetre de dialogue d'UIKit, dont la boucle bloque la fenetre parente.</summary>
	public DialogResult ShowDialog ()
	{
		Ui.Init ();
		string title = base.Text;
		_dialog = Ui.With (title, t => UI.ui_dialog_begin (Width, Height, t));
		if (_dialog == 0)
			return DialogResult.Cancel;
		Application.Dialogs[_dialog] = this;
		UI.ui_dialog_cancel (_dialog, (long) DialogResult.Cancel);
		LayoutChildren ();
		foreach (var c in Controls.Items)
			c.CreateTree ();
		UI.ui_view_flush ();
		delegate* unmanaged<long, long, long, void> fn = &DialogEvent;
		long r = UI.ui_dialog_run (_dialog, (long) fn);
		foreach (var c in Controls.Items)
			c.DestroyTree ();
		UI.ui_dialog_end (_dialog);
		Application.Dialogs.Remove (_dialog);
		_dialog = 0;
		DialogResult = (DialogResult) r;
		return DialogResult;
	}

	[UnmanagedCallersOnly]
	static void DialogEvent (long dialog, long widget, long code)
	{
		try
		{
			if (widget != 0 && Application.Map.TryGetValue (widget, out var c))
				c.Notify (code);
			UI.ui_view_flush ();
		}
		catch (Exception e)
		{
			Application.Fail (e);
		}
	}
}

public static class MessageBox
{
	public static DialogResult Show (string text, string caption = "", MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None)
	{
		Ui.Init ();
		long kind = buttons switch
		{
			MessageBoxButtons.OKCancel => UI.UM_OKCANCEL,
			MessageBoxButtons.YesNo => UI.UM_YESNO,
			MessageBoxButtons.YesNoCancel => UI.UM_YESNOCANCEL,
			_ => UI.UM_OK,
		};
		long a = KitLoader.Utf8 (caption), b = KitLoader.Utf8 (text);
		long r = UI.ui_message (a, b, kind);
		KitLoader.Free (a);
		KitLoader.Free (b);
		bool yes = buttons == MessageBoxButtons.YesNo || buttons == MessageBoxButtons.YesNoCancel;
		return r == 2 ? DialogResult.No : r == 1 ? (yes ? DialogResult.Yes : DialogResult.OK) : (yes && buttons == MessageBoxButtons.YesNo ? DialogResult.No : DialogResult.Cancel);
	}
}

public abstract class FileDialog : IDisposable
{
	public string FileName { get; set; } = "";
	public string InitialDirectory { get; set; }
	public string Title { get; set; }
	public string Filter { get; set; }
	public abstract DialogResult ShowDialog ();
	public void Dispose () { }

	/// <summary>« Texte|*.txt|Tout|*.* » de WinForms -> « *.txt » d'UIKit.</summary>
	protected long FilterPatterns ()
	{
		if (string.IsNullOrEmpty (Filter))
			return 0;
		var parts = Filter.Split ('|');
		var keep = new List<string> ();
		for (int i = 1; i < parts.Length; i += 2)
			if (parts[i] != "*.*" && parts[i] != "*")
				keep.Add (parts[i]);
		return keep.Count == 0 ? 0 : KitLoader.Utf8 (string.Join (";", keep));
	}
}

public unsafe class OpenFileDialog : FileDialog
{
	public override DialogResult ShowDialog ()
	{
		Ui.Init ();
		byte* b = stackalloc byte[512];
		long f = FilterPatterns (), d = InitialDirectory == null ? 0 : KitLoader.Utf8 (InitialDirectory);
		UI.ui_file_filter (f);
		long r = UI.ui_file_open (d, (long) b, 512);
		UI.ui_file_filter (0);
		FileName = KitLoader.String ((long) b);
		return r != 0 ? DialogResult.OK : DialogResult.Cancel;
	}
}

public unsafe class SaveFileDialog : FileDialog
{
	public override DialogResult ShowDialog ()
	{
		Ui.Init ();
		byte* b = stackalloc byte[512];
		long f = FilterPatterns (), d = InitialDirectory == null ? 0 : KitLoader.Utf8 (InitialDirectory);
		long n = KitLoader.Utf8 (FileName);
		UI.ui_file_filter (f);
		long r = UI.ui_file_save (d, n, (long) b, 512);
		UI.ui_file_filter (0);
		KitLoader.Free (n);
		FileName = KitLoader.String ((long) b);
		return r != 0 ? DialogResult.OK : DialogResult.Cancel;
	}
}

public unsafe class FolderBrowserDialog : IDisposable
{
	public string SelectedPath { get; set; } = "";
	public DialogResult ShowDialog ()
	{
		Ui.Init ();
		byte* b = stackalloc byte[512];
		long d = SelectedPath.Length == 0 ? 0 : KitLoader.Utf8 (SelectedPath);
		long r = UI.ui_folder_choose (d, (long) b, 512);
		SelectedPath = KitLoader.String ((long) b);
		return r != 0 ? DialogResult.OK : DialogResult.Cancel;
	}
	public void Dispose () { }
}

public static unsafe class Application
{
	internal static readonly Dictionary<long, Control> Map = new ();
	internal static readonly Dictionary<long, Form> Dialogs = new ();
	internal static readonly List<Form> Forms = new ();
	internal static readonly List<Timer> Timers = new ();
	static readonly Queue<Action> s_posted = new ();
	static int s_thread;
	static Form s_main;
	static bool s_exit;
	static bool s_dirty, s_views;
	static Exception s_fail;

	internal static bool OnUiThread => Environment.CurrentManagedThreadId == s_thread;

	internal static void Post (Action a) { lock (s_posted) s_posted.Enqueue (a); }
	public static void Redraw () => s_dirty = true;
	internal static void ViewsDirty () => s_views = true;
	public static IReadOnlyList<Form> OpenForms => Forms;

	/// <summary>Le temps de a, les ui_* vont a cette fenetre.</summary>
	internal static void InWindow (long window, Action a)
	{
		long old = UI.ui_window_use (window);
		try { a (); }
		finally
		{
			if (old != 0 && old != window)
				UI.ui_window_use (old);
		}
	}

	internal static void ViewCallbacks (long view)
	{
		delegate* unmanaged<long, long, long, void> paint = &ViewPaint;
		delegate* unmanaged<long, long, long, long, long, long> mouse = &ViewMouse;
		delegate* unmanaged<long, long, long, long, long> key = &ViewKey;
		delegate* unmanaged<long, long, long> wheel = &ViewWheel;
		UI.ui_view_on (view, (long) paint, (long) mouse, (long) key, (long) wheel);
		UI.ui_view_dirty (view);
		s_views = true;
	}

	[UnmanagedCallersOnly]
	static void ViewPaint (long view, long w, long h)
	{
		try { if (Map.TryGetValue (view, out var c)) c.ViewPaint ((int) w, (int) h); }
		catch (Exception e) { Fail (e); }
	}

	[UnmanagedCallersOnly]
	static long ViewMouse (long view, long kind, long x, long y, long info)
	{
		try { if (Map.TryGetValue (view, out var c)) c.ViewMouse ((int) kind, (int) x, (int) y, (int) info); }
		catch (Exception e) { Fail (e); }
		return 0;
	}

	[UnmanagedCallersOnly]
	static long ViewKey (long view, long hid, long ch, long mods)
	{
		try { if (Map.TryGetValue (view, out var c) && c.ViewKey ((int) hid, (int) ch, (int) mods)) return 0; }
		catch (Exception e) { Fail (e); }
		return -1;
	}

	[UnmanagedCallersOnly]
	static long ViewWheel (long view, long notches)
	{
		try { if (Map.TryGetValue (view, out var c)) c.ViewWheel ((int) notches); }
		catch (Exception e) { Fail (e); }
		return 0;
	}

	/// <summary>Une fenetre fermee ; la principale se ferme a la fin de Run.</summary>
	internal static void Closed (Form f)
	{
		if (f == s_main)
			return;
		long h = f.Handle;
		Forms.Remove (f);
		foreach (var c in f.Controls.Items)
			c.DestroyTree ();
		Map.Remove (h);
		f.Handle = 0;
		UI.ui_window_close (h);
	}
	internal static void Fail (Exception e) => s_fail = e;

	/// <summary>Le fond de la fenetre, au theme du bureau.</summary>
	internal static void Fill () => UI.ui_fill (0, 0, Ui.Width, Ui.Height, Ui.Token (UI.TJ_FACE));

	public static void EnableVisualStyles () { }
	public static bool SetHighDpiMode (HighDpiMode mode) => true;	// (UIKit a sa propre echelle)
	public static void SetCompatibleTextRenderingDefault (bool value) { }
	public static void Exit () => s_exit = true;

	public static void DoEvents ()
	{
		Pump (0);
	}

	[UnmanagedCallersOnly]
	internal static void Resized (long w, long h)
	{
		try { FormOf (Ui.Event (UI.EV_WIN))?.ClientResized (w, h); }
		catch (Exception e) { Fail (e); }
	}

	static Form FormOf (long window) => Map.TryGetValue (window, out var c) ? c as Form : s_main;

	public static void Run (Form form)
	{
		s_thread = Environment.CurrentManagedThreadId;
		s_main = form;
		form.Open ();
		while (!s_exit && !form.Closed)
		{
			long wait = 200, now = Environment.TickCount64;
			foreach (var t in Timers.ToArray ())
				wait = Math.Min (wait, Math.Max (0, t.Due (now)));
			lock (s_posted)
				if (s_posted.Count > 0)
					wait = 0;
			Pump (wait);
		}
		foreach (var f in Forms.ToArray ())
			UI.ui_window_close (f.Handle);
		Forms.Clear ();
	}

	static void Pump (long wait)
	{
		for (;;)
		{
			Action a;
			lock (s_posted)
			{
				if (s_posted.Count == 0)
					break;
				a = s_posted.Dequeue ();
			}
			a ();
		}
		long type = UI.ui_event (wait);
		if (s_fail != null)
			throw s_fail;
		if (type == 0)
		{
			UI.ui_w_idle ();
		}
		else if (type == UI.WEV_CLOSE)
		{
			FormOf (Ui.Event (UI.EV_WIN))?.Close ();
		}
		else if (type == UI.WEV_MENU)
		{
			ToolStripMenuItem.Fire (Ui.Event (UI.EV_CODE));
		}
		else
		{
			bool press = type == UI.WEV_KEY && Ui.Event (UI.EV_KIND) != 0;
			if (!press || FormOf (Ui.Event (UI.EV_WIN)) is not { KeyPreview: true } preview || !preview.PreviewKey ())
			{
				UI.ui_w_event (0, out long w, out long code, out long taken);
				if (w != 0 && code != 0 && Map.TryGetValue (w, out var c))
					c.Notify (code);
				if (press && taken == 0)
				{
					long id = UI.ui_menu_key (0);
					if (id >= 0)
						ToolStripMenuItem.Fire ((int) id);
				}
			}
		}
		if (s_dirty)
		{
			s_dirty = false;
			foreach (var f in Forms)
				InWindow (f.Handle, () =>
				{
					Fill ();
					UI.ui_w_draw (0);
					UI.ui_update_all ();
				});
		}
		if (s_views)
		{
			s_views = false;
			foreach (var f in Forms)
				InWindow (f.Handle, () => UI.ui_view_flush ());
		}
	}
}
