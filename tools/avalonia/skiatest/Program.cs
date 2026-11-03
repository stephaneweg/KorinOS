// skiatest : le test decisif sans fenetre (docs/AVALONIA.md). Compile une fois sur le PC, il dessine
// avec SkiaSharp dans un SKBitmap, ecrit un PNG, le relit et compare, et met HarfBuzzSharp a l'epreuve.
//     dotnet skiatest.dll sortie.png
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using HarfBuzzSharp;
using SkiaSharp;

static class Program
{
	static int Main (string[] args)
	{
		string path = args.Length > 0 ? args[0] : "skiatest.png";
		Console.WriteLine ("SkiaSharp : natif " + SkiaSharpVersion.Native + ", minimum " + SkiaSharpVersion.NativeMinimum);
		Console.WriteLine ("OS : " + System.Runtime.InteropServices.RuntimeInformation.OSDescription);

		using var bmp = new SKBitmap (new SKImageInfo (640, 400, SKColorType.Bgra8888, SKAlphaType.Premul));
		using (var c = new SKCanvas (bmp))
		{
			c.Clear (new SKColor (0xF4, 0xF6, 0xFA));
			using var grad = SKShader.CreateLinearGradient (new SKPoint (0, 0), new SKPoint (640, 0),
				new[] { new SKColor (0x20, 0x60, 0xE0), new SKColor (0xE0, 0x40, 0x80) }, SKShaderTileMode.Clamp);
			using var fill = new SKPaint { Shader = grad, IsAntialias = true };
			c.DrawRoundRect (new SKRoundRect (new SKRect (20, 20, 620, 110), 16), fill);
			using var stroke = new SKPaint { Color = new SKColor (0x10, 0xA0, 0x60), IsAntialias = true, IsStroke = true, StrokeWidth = 4 };
			c.DrawCircle (120, 240, 80, stroke);
			using var path2 = new SKPath ();
			path2.MoveTo (240, 320);
			path2.CubicTo (300, 120, 420, 340, 600, 170);
			using var line = new SKPaint { Color = SKColors.DarkOrange, IsAntialias = true, IsStroke = true, StrokeWidth = 6, StrokeCap = SKStrokeCap.Round };
			c.DrawPath (path2, line);
			using var blur = new SKPaint { Color = new SKColor (0, 0, 0, 90), ImageFilter = SKImageFilter.CreateBlur (6, 6), IsAntialias = true };
			c.DrawRect (new SKRect (440, 250, 600, 330), blur);

			var tf = SKFontManager.Default.MatchFamily ("DejaVu Sans") ?? SKTypeface.Default;
			Console.WriteLine ("police : " + tf.FamilyName + ", " + SKFontManager.Default.FontFamilyCount + " familles");
			using var font = new SKFont (tf, 28);
			using var white = new SKPaint { Color = SKColors.White, IsAntialias = true };
			c.DrawText ("SkiaSharp sous ARMPI", 44, 76, font, white);

			// le texte passe par HarfBuzz, comme dans Avalonia
			using var stream = tf.OpenStream ();
			using var fontdata = SKData.Create (stream);
			using var blob = new Blob (fontdata.Data, (int) fontdata.Size, MemoryMode.ReadOnly);
			using var face = new Face (blob, 0);
			using var hbfont = new HarfBuzzSharp.Font (face);
			using var buf = new HarfBuzzSharp.Buffer ();
			buf.AddUtf16 ("Été ffi AVA");
			buf.GuessSegmentProperties ();
			hbfont.Shape (buf);
			Console.WriteLine ("HarfBuzz : " + buf.Length + " glyphes, " + string.Join (" ", buf.GlyphInfos.Select (g => g.Codepoint)));
			using var small = new SKFont (tf, 20);
			using var ink = new SKPaint { Color = new SKColor (0x20, 0x20, 0x30), IsAntialias = true };
			using var tb = new SKTextBlobBuilder ();
			var run = tb.AllocatePositionedRun (small, buf.Length);
			float x = 0;
			var infos = buf.GlyphInfos;
			var pos = buf.GlyphPositions;
			hbfont.GetScale (out int xs, out int ys);
			for (int i = 0; i < infos.Length; i++)
			{
				run.Glyphs[i] = (ushort) infos[i].Codepoint;
				run.Positions[i] = new SKPoint (x, 0);
				x += pos[i].XAdvance * 20f / xs;
			}
			using var built = tb.Build ();
			c.DrawText (built, 240, 150, ink);
		}

		using (var img = SKImage.FromBitmap (bmp))
		using (var data = img.Encode (SKEncodedImageFormat.Png, 100))
		using (var fs = File.Create (path))
			data.SaveTo (fs);

		var bytes = File.ReadAllBytes (path);
		Console.WriteLine ("PNG : " + bytes.Length + " octets, sha256 " + Convert.ToHexString (SHA256.HashData (bytes)).ToLowerInvariant ());
		using var back = SKBitmap.Decode (path);
		bool same = back != null && back.Width == bmp.Width && back.Height == bmp.Height;
		int diff = 0;
		if (same)
			for (int y = 0; y < bmp.Height; y += 7)
				for (int x = 0; x < bmp.Width; x += 7)
					if (back.GetPixel (x, y) != bmp.GetPixel (x, y))
						diff++;
		var sb = new StringBuilder ();
		sb.Append ("pixels : ").Append (Hash (bmp)).Append (", relu ").Append (same ? "oui" : "non").Append (", ecarts ").Append (diff);
		Console.WriteLine (sb);
		Console.WriteLine (same && diff == 0 ? "skiatest : ok" : "skiatest : ECHEC");
		return same && diff == 0 ? 0 : 1;
	}

	static string Hash (SKBitmap b)
	{
		return Convert.ToHexString (SHA256.HashData (b.GetPixelSpan ())).ToLowerInvariant ().Substring (0, 16);
	}
}
