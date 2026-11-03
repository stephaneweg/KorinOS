// ArmpiServices.cs -- le reste de la plateforme : curseurs, presse-papiers, ecran, theme, clavier.
#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Kits;
using System.Threading.Tasks;
using System.UIKit;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Themes.Fluent;

namespace Avalonia.Armpi
{
	internal sealed class ArmpiCursor : ICursorImpl
	{
		public readonly int Form;
		public ArmpiCursor (int form) => Form = form;
		public void Dispose () { }
	}

	internal sealed class ArmpiCursorFactory : ICursorFactory
	{
		public ICursorImpl GetCursor (StandardCursorType type) => new ArmpiCursor (type switch
		{
			StandardCursorType.Ibeam => (int) UI.UC_TEXT,
			StandardCursorType.Hand => (int) UI.UC_HAND,
			StandardCursorType.Wait or StandardCursorType.AppStarting => (int) UI.UC_WAIT,
			StandardCursorType.Cross => (int) UI.UC_CROSS,
			StandardCursorType.No => (int) UI.UC_NO,
			StandardCursorType.SizeAll => (int) UI.UC_MOVE,
			StandardCursorType.SizeWestEast or StandardCursorType.LeftSide or StandardCursorType.RightSide => (int) UI.UC_HRESIZE,
			StandardCursorType.SizeNorthSouth or StandardCursorType.TopSide or StandardCursorType.BottomSide => (int) UI.UC_VRESIZE,
			StandardCursorType.TopLeftCorner or StandardCursorType.BottomRightCorner => (int) UI.UC_NWSE,
			StandardCursorType.TopRightCorner or StandardCursorType.BottomLeftCorner => (int) UI.UC_NESW,
			_ => (int) UI.UC_ARROW,
		});
		public ICursorImpl CreateCursor (Bitmap cursor, PixelPoint hotSpot) => new ArmpiCursor ((int) UI.UC_ARROW);
	}

	internal sealed class ArmpiIconLoader : IPlatformIconLoader
	{
		sealed class Icon : IWindowIconImpl
		{
			public void Save (Stream outputStream) { }
		}
		public IWindowIconImpl LoadIcon (string fileName) => new Icon ();
		public IWindowIconImpl LoadIcon (Stream stream) => new Icon ();
		public IWindowIconImpl LoadIcon (IBitmapImpl bitmap) => new Icon ();
	}

	/// <summary>Le presse-papiers d'Elegant, en texte.</summary>
	internal sealed class ArmpiClipboard : IClipboardImpl
	{
		static unsafe string? Read ()
		{
			var buffer = new byte[1 << 20];
			fixed (byte* p = buffer)
				return UI.ui_clip_get ((long) p, buffer.Length) > 0 ? KitLoader.String ((long) p) : null;
		}

		public Task<IAsyncDataTransfer?> TryGetDataAsync ()
		{
			var text = Read ();
			if (string.IsNullOrEmpty (text))
				return Task.FromResult<IAsyncDataTransfer?> (null);
			var d = new DataTransfer ();
			d.Add (DataTransferItem.CreateText (text));
			return Task.FromResult<IAsyncDataTransfer?> (d);
		}

		public async Task SetDataAsync (IAsyncDataTransfer dataTransfer)
		{
			var text = await dataTransfer.TryGetTextAsync ();
			Ui.With (text ?? "", p => UI.ui_clip_set (p, -1));
		}

		public Task ClearAsync ()
		{
			Ui.With ("", p => UI.ui_clip_set (p, -1));
			return Task.CompletedTask;
		}
	}

	internal sealed class ArmpiScreen : PlatformScreen
	{
		public ArmpiScreen (PixelRect bounds, PixelRect work) : base (new PlatformHandle (IntPtr.Zero, "Elegant"))
		{
			Bounds = bounds;
			WorkingArea = work;
			Scaling = 1;
			IsPrimary = true;
			DisplayName = "Elegant";
		}
	}

	/// <summary>Un seul ecran.</summary>
	internal sealed unsafe class ArmpiScreens : ScreensBase<int, ArmpiScreen>
	{
		public static readonly ArmpiScreens Instance = new ();

		internal PixelSize Size
		{
			get
			{
				long* s = stackalloc long[4];
				return UI.ui_screen ((long) s) < 0 ? new PixelSize (1024, 768) : new PixelSize ((int) s[0], (int) s[1]);
			}
		}

		internal PixelRect WorkArea
		{
			get
			{
				long* s = stackalloc long[4];
				if (UI.ui_screen ((long) s) < 0)
					return new PixelRect (0, 0, 1024, 768);
				return new PixelRect (0, (int) s[3], (int) s[0], (int) (s[1] - s[2] - s[3]));
			}
		}

		protected override IReadOnlyList<int> GetAllScreenKeys () => new[] { 0 };

		protected override ArmpiScreen CreateScreenFromKey (int key)
		{
			var work = WorkArea;
			return new ArmpiScreen (new PixelRect (0, 0, work.Width, work.Bottom), work);
		}
	}

	/// <summary>Clair ou sombre, et l'accent : les jetons du theme du bureau (TJ_*).</summary>
	internal sealed class ArmpiSettings : DefaultPlatformSettings
	{
		public override PlatformColorValues GetColorValues ()
		{
			uint face = Ui.Token (UI.TJ_FACE), accent = Ui.Token (UI.TJ_ACCENT);
			int lum = (int) (((face >> 16) & 255) * 3 + ((face >> 8) & 255) * 6 + (face & 255)) / 10;
			return new PlatformColorValues
			{
				ThemeVariant = lum < 128 ? PlatformThemeVariant.Dark : PlatformThemeVariant.Light,
				AccentColor1 = Color.FromRgb ((byte) (accent >> 16), (byte) (accent >> 8), (byte) accent),
			};
		}
	}

	/// <summary>Fluent aux couleurs du bureau : les jetons d'UIKit dans sa palette (FluentTheme.Palettes).</summary>
	internal static class ArmpiTheme
	{
		static Color Token (long offset, byte alpha = 255)
		{
			uint c = Ui.Token (offset);
			return Color.FromArgb (alpha, (byte) (c >> 16), (byte) (c >> 8), (byte) c);
		}

		internal static void Apply (AppBuilder builder)
		{
			foreach (var style in builder.Instance!.Styles)
			{
				if (style is not FluentTheme fluent)
					continue;
				fluent.Palettes[Styling.ThemeVariant.Light] = Palette ();
				fluent.Palettes[Styling.ThemeVariant.Dark] = Palette ();
			}
		}

		static ColorPaletteResources Palette () => new ()
		{
			Accent = Token (UI.TJ_ACCENT), RegionColor = Token (UI.TJ_FACE), ErrorText = Token (UI.TJ_DANGER),
			AltHigh = Token (UI.TJ_SURFACE), AltMediumHigh = Token (UI.TJ_SURFACE, 0xCC), AltMedium = Token (UI.TJ_SURFACE, 0x99),
			AltMediumLow = Token (UI.TJ_SURFACE, 0x66), AltLow = Token (UI.TJ_SURFACE, 0x33),
			BaseHigh = Token (UI.TJ_TEXTE1), BaseMediumHigh = Token (UI.TJ_TEXTE1, 0xCC), BaseMedium = Token (UI.TJ_TEXTE1, 0x99),
			BaseMediumLow = Token (UI.TJ_TEXTE1, 0x66), BaseLow = Token (UI.TJ_TEXTE1, 0x33),
			ChromeLow = Token (UI.TJ_CREUX), ChromeMediumLow = Token (UI.TJ_CREUX), ChromeMedium = Token (UI.TJ_ELEVE),
			ChromeHigh = Token (UI.TJ_LIGNE_FORTE), ChromeAltLow = Token (UI.TJ_TEXTE1), ChromeWhite = Token (UI.TJ_SURFACE),
			ListLow = Token (UI.TJ_TEXTE1, 0x19), ListMedium = Token (UI.TJ_TEXTE1, 0x33),
		};
	}

	/// <summary>Les touches : codes HID d'Elegant. La lettre vient du caractere, l'OS est en AZERTY.</summary>
	internal static class ArmpiKeys
	{
		internal static RawInputModifiers Modifiers (int mods)
		{
			var m = RawInputModifiers.None;
			if ((mods & 0x22) != 0) m |= RawInputModifiers.Shift;
			if ((mods & 0x11) != 0) m |= RawInputModifiers.Control;
			if ((mods & 0x44) != 0) m |= RawInputModifiers.Alt;
			if ((mods & 0x88) != 0) m |= RawInputModifiers.Meta;
			return m;
		}

		internal static Key ToKey (int hid, int ch)
		{
			if (ch is >= 'a' and <= 'z') return Key.A + (ch - 'a');
			if (ch is >= 'A' and <= 'Z') return Key.A + (ch - 'A');
			if (ch is >= 1 and <= 26 && hid is >= 4 and <= 0x1D) return Key.A + (ch - 1);
			if (hid is >= 4 and <= 0x1D) return Azerty (hid);
			if (hid is >= 0x1E and <= 0x26) return Key.D1 + (hid - 0x1E);
			if (hid is >= 0x3A and <= 0x45) return Key.F1 + (hid - 0x3A);
			return hid switch
			{
				0x27 => Key.D0, 0x28 => Key.Return, 0x29 => Key.Escape, 0x2A => Key.Back, 0x2B => Key.Tab, 0x2C => Key.Space,
				0x49 => Key.Insert, 0x4A => Key.Home, 0x4B => Key.PageUp, 0x4C => Key.Delete, 0x4D => Key.End, 0x4E => Key.PageDown,
				0x4F => Key.Right, 0x50 => Key.Left, 0x51 => Key.Down, 0x52 => Key.Up, 0x58 => Key.Return,
				0xE0 => Key.LeftCtrl, 0xE1 => Key.LeftShift, 0xE2 => Key.LeftAlt, 0xE3 => Key.LWin,
				0xE4 => Key.RightCtrl, 0xE5 => Key.RightShift, 0xE6 => Key.RightAlt, 0xE7 => Key.RWin,
				_ => Key.None,
			};
		}

		static Key Azerty (int hid) => hid switch
		{
			0x14 => Key.A, 0x04 => Key.Q, 0x1D => Key.W, 0x1A => Key.Z,
			_ => Key.A + (hid - 4),
		};

		internal static PhysicalKey ToPhysical (int hid)
		{
			if (hid is >= 4 and <= 0x1D) return PhysicalKey.A + (hid - 4);
			if (hid is >= 0x1E and <= 0x26) return PhysicalKey.Digit1 + (hid - 0x1E);
			if (hid is >= 0x3A and <= 0x45) return PhysicalKey.F1 + (hid - 0x3A);
			return hid switch
			{
				0x27 => PhysicalKey.Digit0, 0x28 => PhysicalKey.Enter, 0x29 => PhysicalKey.Escape, 0x2A => PhysicalKey.Backspace,
				0x2B => PhysicalKey.Tab, 0x2C => PhysicalKey.Space, 0x49 => PhysicalKey.Insert, 0x4A => PhysicalKey.Home,
				0x4B => PhysicalKey.PageUp, 0x4C => PhysicalKey.Delete, 0x4D => PhysicalKey.End, 0x4E => PhysicalKey.PageDown,
				0x4F => PhysicalKey.ArrowRight, 0x50 => PhysicalKey.ArrowLeft, 0x51 => PhysicalKey.ArrowDown, 0x52 => PhysicalKey.ArrowUp,
				0xE0 => PhysicalKey.ControlLeft, 0xE1 => PhysicalKey.ShiftLeft, 0xE2 => PhysicalKey.AltLeft, 0xE3 => PhysicalKey.MetaLeft,
				0xE4 => PhysicalKey.ControlRight, 0xE5 => PhysicalKey.ShiftRight, 0xE6 => PhysicalKey.AltRight, 0xE7 => PhysicalKey.MetaRight,
				_ => PhysicalKey.None,
			};
		}
	}
}
