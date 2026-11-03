// Drawing.cs -- System.Drawing.Common d'ARMPI, le dessin de OnPaint, sur les ui_* d'UIKit : assembly a part
// sous le nom de celui de Windows, un binaire compile y cherche Graphics, Pen, Font (docs/WINFORMS-PORTABLE.md). Coordonnees en pixels de la vue, formes anticrenelees (ui_vp_*). Les couleurs du bureau sont
// dans ThemeColors : un controle dessine a la main les prend, pour suivre le theme.
#nullable disable
using System.Reflection;
using System.Runtime.CompilerServices;
using System.UIKit;

[assembly: AssemblyVersion ("10.0.0.0")]
[assembly: InternalsVisibleTo ("System.Windows.Forms, PublicKey=00000000000000000400000000000000")]

namespace System.Drawing;

public sealed class Pen : MarshalByRefObject, IDisposable
{
	public Color Color { get; set; }
	public float Width { get; set; }
	public Pen (Color color) : this (color, 1) { }
	public Pen (Color color, float width) { Color = color; Width = width; }
	public void Dispose () { }
}

public abstract class Brush : MarshalByRefObject, IDisposable
{
	public void Dispose () { }
}

public sealed class SolidBrush : Brush
{
	public Color Color { get; set; }
	public SolidBrush (Color color) { Color = color; }
}

public abstract class Image : MarshalByRefObject, IDisposable
{
	internal long Handle;
	internal Image (long h) { Handle = h; }
	public void Dispose () { }
	/// <summary>Une image BMP ou PNG en memoire ; null : illisible.</summary>
	public static unsafe Image FromBytes (byte[] data)
	{
		fixed (byte* p = data)
		{
			long h = UI.ui_image_decode ((long) p, data.Length);
			return h == 0 ? null : new Bitmap (h);
		}
	}
	public static Image FromFile (string path)
	{
		long h = Ui.With (path, p => UI.ui_image_load (p));
		if (h == 0)
			throw new System.IO.FileNotFoundException ("image illisible", path);
		return new Bitmap (h);
	}
}

public sealed class Bitmap : Image
{
	internal Bitmap (long h) : base (h) { }
}

public sealed unsafe class Graphics : MarshalByRefObject, IDisposable
{
	readonly int _w, _h;
	internal Graphics (int w, int h) { _w = w; _h = h; }
	public void Dispose () { }

	static long Rgb (Color c) => c.ToArgb () & 0xFFFFFF;
	static long Rgb (Brush b) => b is SolidBrush s ? Rgb (s.Color) : 0;

	public void Clear (Color c) => UI.ui_fill (0, 0, _w, _h, Rgb (c));
	public void FillRectangle (Brush b, int x, int y, int w, int h) => UI.ui_fill (x, y, w, h, Rgb (b));
	public void FillRectangle (Brush b, Rectangle r) => FillRectangle (b, r.X, r.Y, r.Width, r.Height);
	public void DrawRectangle (Pen p, int x, int y, int w, int h) => UI.ui_frame (x, y, w, h, Rgb (p.Color));
	public void DrawRectangle (Pen p, Rectangle r) => DrawRectangle (p, r.X, r.Y, r.Width, r.Height);

	public void DrawLine (Pen p, int x1, int y1, int x2, int y2) =>
		UI.ui_vp_line (x1 * 16L + 8, y1 * 16L + 8, x2 * 16L + 8, y2 * 16L + 8, (long) (p.Width * 16), Rgb (p.Color));
	public void DrawLine (Pen p, Point a, Point b) => DrawLine (p, a.X, a.Y, b.X, b.Y);

	public void FillPolygon (Brush b, Point[] points)
	{
		int* v = stackalloc int[points.Length * 2];
		for (int i = 0; i < points.Length; i++)
		{
			v[2 * i] = points[i].X * 16;
			v[2 * i + 1] = points[i].Y * 16;
		}
		UI.ui_vp_poly ((long) v, points.Length, Rgb (b));
	}

	public void DrawPolygon (Pen p, Point[] points)
	{
		for (int i = 0; i < points.Length; i++)
			DrawLine (p, points[i], points[(i + 1) % points.Length]);
	}

	static Point[] Oval (int x, int y, int w, int h)
	{
		var v = new Point[48];
		for (int i = 0; i < v.Length; i++)
		{
			double a = 2 * Math.PI * i / v.Length;
			v[i] = new Point (x + (int) Math.Round (w / 2.0 * (1 + Math.Cos (a))), y + (int) Math.Round (h / 2.0 * (1 + Math.Sin (a))));
		}
		return v;
	}
	public void FillEllipse (Brush b, int x, int y, int w, int h) => FillPolygon (b, Oval (x, y, w, h));
	public void DrawEllipse (Pen p, int x, int y, int w, int h) => DrawPolygon (p, Oval (x, y, w, h));

	public void DrawString (string s, Font font, Brush b, float x, float y)
	{
		font ??= Font.Default;
		Ui.With (s, t => UI.ui_text (t, (long) x, (long) y + font.Pixels, Rgb (b), font.Pixels, font.Handle, 0));
	}
	/// <summary>Le texte coupe a cette largeur.</summary>
	public void DrawString (string s, Font font, Brush b, int x, int y, int maxWidth)
	{
		font ??= Font.Default;
		Ui.With (s, t => UI.ui_text_fit (t, x, y + font.Pixels, Rgb (b), font.Pixels, font.Handle, maxWidth));
	}
	public SizeF MeasureString (string s, Font font)
	{
		font ??= Font.Default;
		return new SizeF (font.Width (s), font.Pixels * 1.3f);
	}

	public void DrawImage (Image i, int x, int y) => UI.ui_image_draw (i.Handle, x, y, 0, 0);
	public void DrawImage (Image i, int x, int y, int w, int h) => UI.ui_image_draw (i.Handle, x, y, w, h);
	public void SetClip (Rectangle r) => UI.ui_clip (r.X, r.Y, r.Width, r.Height);
	public void ResetClip () => UI.ui_clip (0, 0, 0, 0);
}
