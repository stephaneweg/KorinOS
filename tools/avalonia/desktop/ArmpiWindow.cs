// ArmpiWindow.cs -- une fenetre d'Elegant et sa surface attachee (ui_surface) : Skia peint dans un tampon prive, recopie d'un bloc dans la surface : Avalonia efface avant de peindre, Elegant ne doit pas le voir ;
// Elegant compose ce tampon sans recopie. Elegant ne cache pas une fenetre : elle n'existe qu'entre Show
// et Hide. Les 32 px du haut restent au cadre (les boutons de la fenetre, son deplacement) : le contenu
// d'Avalonia commence dessous, sauf avec ExtendClientAreaToDecorationsHint, comme une app native : une barre
// d'outils y prend place, en laissant libres les boutons du cadre, a droite (FRAME_CAP_W). Sur le GPU
// (ArmpiGl.cs), le rendu va dans une surface de Mesa, et celle-ci n'existe pas.
#nullable enable
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.UIKit;
using Avalonia.Controls;
using Avalonia.Controls.Platform;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Input.Raw;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Platform.Surfaces;
using Avalonia.Rendering.Composition;

namespace Avalonia.Armpi
{
	internal sealed class ArmpiWindow : IWindowImpl, IFramebufferPlatformSurface
	{
		const int CaptionHeight = 32;
		long _win, _surface, _pixels, _pitch, _minWidth, _minHeight;
		unsafe void* _back;
		nuint _backSize;
		int _width = 800, _height = 450, _top = CaptionHeight;
		int _x = -1, _y = -1, _buttons, _mx, _my;
		bool _retained, _canResize = true, _closed, _enabled = true, _frameless, _extended;
		string _title = "";
		WindowState _state = WindowState.Normal;
		IInputRoot? _root;
		ArmpiStorage? _storage;
		readonly MouseDevice _mouse = new ();
		readonly IKeyboardDevice _keyboard = AvaloniaLocator.Current.GetRequiredService<IKeyboardDevice> ();

		IntPtr _gl;

		public ArmpiWindow ()
		{
			Surfaces = ArmpiPlatform.Gl != null ? new IPlatformRenderSurface[] { new ArmpiGlSurface (this), this } : new IPlatformRenderSurface[] { this };
		}

		internal long ZOrder { get; private set; }

		internal void InWindow (Action call)
		{
			long old = UI.ui_window_use (_win);
			try { call (); }
			finally
			{
				if (old != 0 && old != _win)
					UI.ui_window_use (old);
			}
		}

		// --- la fenetre -------------------------------------------------------------------------

		public void Show (bool activate, bool isDialog)
		{
			if (_win == 0)
			{
				long role = (_canResize ? UI.WF_RESIZABLE : 0) | (isDialog ? UI.WF_MODAL : 0) | (_frameless ? UI.WF_FRAMELESS : 0);
				_win = Ui.With (_title, t => UI.ui_window_ex (_x, _y, _width, _height + _top, t, role));
				if (_win <= 0)
				{
					_win = 0;
					throw new InvalidOperationException ("Elegant refuse la fenetre");
				}
				ArmpiPlatform.Windows[_win] = this;
				ApplyMinSize ();
				PaintCaption ();
				AttachSurface ();
			}
			if (activate)
				Activate ();
		}

		public void Hide ()
		{
			CloseNative ();
			Deactivated?.Invoke ();
		}

		void PaintCaption ()
		{
			InWindow (() =>
			{
				if (_top > 0)
					UI.ui_fill (0, 0, Ui.Width, _top, Ui.Token (UI.TJ_FACE));
				UI.ui_update_all ();
			});
		}

		unsafe void AttachSurface ()
		{
			if (ArmpiPlatform.Gl != null)
			{
				InWindow (() =>
				{
					UI.ui_fill (0, _top, _width, _height, Ui.Token (UI.TJ_FACE));	// (avant la premiere image)
					UI.ui_update_all ();
				});
				return;
			}
			InWindow (() =>
			{
				if (_surface == 0)
					_surface = UI.ui_surface (0, _top, _width, _height, UI.WS_SF_XRGB8888);
				else if (UI.ui_surface_move (_surface, 0, _top, _width, _height) == 0)
				{
					UI.ui_surface_free (_surface);
					_surface = UI.ui_surface (0, _top, _width, _height, UI.WS_SF_XRGB8888);
				}
				if (_surface == 0)
					throw new InvalidOperationException ("Elegant refuse la surface");
				UI.ui_surface_pixels (_surface, out _pixels, out _pitch, out long _, out long _);
				new Span<uint> ((void*) _pixels, (int) (_pitch / 4) * _height).Fill (Ui.Token (UI.TJ_FACE));	// (avant la premiere image)
				UI.ui_surface_damage (_surface, 0, 0, 0, 0);
				_backSize = (nuint) (_pitch * _height);
				_back = NativeMemory.Realloc (_back, _backSize);
				_retained = false;
			});
		}

		void CloseNative ()
		{
			if (_win == 0)
				return;
			InWindow (() =>
			{
				if (_surface != 0)
					UI.ui_surface_free (_surface);
			});
			_surface = 0;
			if (_gl != IntPtr.Zero)
			{
				ArmpiPlatform.Gl!.GiveBack (_gl);
				_gl = IntPtr.Zero;
			}
			unsafe
			{
				NativeMemory.Free (_back);
				_back = null;
			}
			ArmpiPlatform.Windows.Remove (_win);
			UI.ui_window_close (_win);
			_win = 0;
		}

		public void Dispose ()
		{
			if (_closed)
				return;
			_closed = true;
			CloseNative ();
			Closed?.Invoke ();
		}

		public void Activate ()
		{
			ZOrder = ++ArmpiPlatform.ZCounter;
			if (_win != 0)
				UI.ui_raise (_win);
			Activated?.Invoke ();
		}

		public void Resize (Size clientSize, WindowResizeReason reason = WindowResizeReason.Application)
		{
			int w = Math.Max (1, (int) Math.Ceiling (clientSize.Width)), h = Math.Max (1, (int) Math.Ceiling (clientSize.Height));
			if (w == _width && h == _height)
				return;
			_width = w;
			_height = h;
			if (_win != 0)
			{
				InWindow (() => UI.ui_resize (w, h + _top));
				PaintCaption ();
				AttachSurface ();
			}
			Resized?.Invoke (new Size (w, h), reason);
		}

		public void SetTitle (string? title)
		{
			_title = title ?? "";
			if (_win != 0)
				InWindow (() => Ui.With (_title, t => { UI.ui_title (t); return 0; }));
		}

		public void Move (PixelPoint point)
		{
			_x = point.X;
			_y = point.Y;
			PositionChanged?.Invoke (point);
		}

		public void SetMinMaxSize (Size minSize, Size maxSize)
		{
			_minWidth = (long) minSize.Width;
			_minHeight = (long) minSize.Height;
			ApplyMinSize ();
		}

		void ApplyMinSize ()
		{
			if (_win != 0 && (_minWidth > 0 || _minHeight > 0))
				InWindow (() => UI.ui_min_size (_minWidth, _minHeight + _top));
		}

		public void CanResize (bool value) => _canResize = value;
		public void SetWindowDecorations (WindowDecorations enabled)
		{
			_frameless = enabled == WindowDecorations.None;
			UpdateTop ();
		}

		void UpdateTop ()
		{
			int top = _frameless || _extended ? 0 : CaptionHeight;
			if (top == _top)
				return;
			int height = _height + _top - top;			// (la fenetre garde sa taille, le contenu la suit)
			_top = top;
			_height = height;
			if (_win != 0)
			{
				PaintCaption ();
				AttachSurface ();
				Resized?.Invoke (new Size (_width, _height), WindowResizeReason.Layout);
			}
		}

		public void SetEnabled (bool enable) => _enabled = enable;
		public void SetParent (IWindowImpl? parent) { }
		public void SetIcon (IWindowIconImpl? icon) { }
		public void ShowTaskbarIcon (bool value) { }
		public void SetCanMinimize (bool value) { }
		public void SetCanMaximize (bool value) { }
		public void SetTopmost (bool value) { }
		public void SetExtendClientAreaToDecorationsHint (bool extendIntoClientAreaHint)
		{
			if (_extended == extendIntoClientAreaHint)
				return;
			_extended = extendIntoClientAreaHint;
			UpdateTop ();
			ExtendClientAreaToDecorationsChanged?.Invoke (_extended);
		}

		public void SetExtendClientAreaTitleBarHeightHint (double titleBarHeight) { }
		public void BeginMoveDrag (PointerPressedEventArgs e) { if (_win != 0) InWindow (() => UI.ui_window_drag ()); }
		public void BeginResizeDrag (WindowEdge edge, PointerPressedEventArgs e) { }
		public void SetTransparencyLevelHint (IReadOnlyList<WindowTransparencyLevel> transparencyLevels) { }
		public void SetFrameThemeVariant (PlatformThemeVariant? themeVariant) { }

		public WindowState WindowState
		{
			get => _state;
			set
			{
				if (value == WindowState.Maximized != (_state == WindowState.Maximized) && _win != 0)
					InWindow (() => UI.ui_maximize ());
				_state = value;
			}
		}
		public bool WindowStateGetterIsUsable => false;
		public Action<WindowState>? WindowStateChanged { get; set; }

		public double DesktopScaling => 1;
		public double RenderScaling => 1;
		public IPlatformHandle? Handle => null;
		public Size ClientSize => new (_width, _height);
		public Size? FrameSize => new Size (_width, _height + _top);
		public Size MaxAutoSizeHint
		{
			get
			{
				var r = ArmpiScreens.Instance.WorkArea;
				return new Size (r.Width, r.Height - _top);
			}
		}
		public unsafe PixelPoint Position
		{
			get
			{
				if (_win == 0)
					return new (Math.Max (_x, 0), Math.Max (_y, 0));
				const int Page = 7;
				byte* e = stackalloc byte[Page * (int) UI.WE_SIZE];
				long count = 1;
				for (long first = 0; first < count; first += Page)
				{
					count = UI.ui_win_details (first, (long) e);
					for (int i = 0; i < Page && first + i < count; i++)
					{
						byte* w = e + i * UI.WE_SIZE;
						if (*(uint*) (w + UI.WE_ID) == (uint) _win)
							return new (*(short*) (w + UI.WE_X), *(short*) (w + UI.WE_Y));
					}
				}
				return default;
			}
		}
		public Compositor Compositor => ArmpiPlatform.Compositor;
		public IPlatformRenderSurface[] Surfaces { get; }
		public IPopupImpl? CreatePopup () => null;		// (les popups restent dans la fenetre)
		public WindowTransparencyLevel TransparencyLevel => WindowTransparencyLevel.None;
		public AcrylicPlatformCompensationLevels AcrylicCompensationLevels { get; } = new (1, 1, 1);
		public bool IsClientAreaExtendedToDecorations => _extended;
		public bool NeedsManagedDecorations => false;
		public PlatformRequestedDrawnDecoration RequestedDrawnDecorations => default;
		public Thickness ExtendedMargins => _extended ? new Thickness (0, CaptionHeight, 0, 0) : default;
		public Thickness OffScreenMargin => default;
		public Point PointToClient (PixelPoint point) => (point - Position).ToPoint (1);
		public PixelPoint PointToScreen (Point point) => PixelPoint.FromPoint (point, 1) + Position;
		public void SetInputRoot (IInputRoot inputRoot) => _root = inputRoot;

		public void SetCursor (ICursorImpl? cursor)
		{
			int form = (cursor as ArmpiCursor)?.Form ?? 0;
			if (_win != 0)
				InWindow (() => UI.ui_cursor (form));
		}

		public object? TryGetFeature (Type featureType)
		{
			if (featureType == typeof (IClipboard))
				return AvaloniaLocator.Current.GetService<IClipboard> ();
			if (featureType == typeof (IScreenImpl))
				return ArmpiScreens.Instance;
			if (featureType == typeof (IStorageProvider))
				return _storage ??= new ArmpiStorage (this);
			return null;
		}

		public Action<RawInputEventArgs>? Input { get; set; }
		public Action<Rect>? Paint { get; set; }
		public Action<Size, WindowResizeReason>? Resized { get; set; }
		public Action<double>? ScalingChanged { get; set; }
		public Action<WindowTransparencyLevel>? TransparencyLevelChanged { get; set; }
		public Action? Closed { get; set; }
		public Action? LostFocus { get; set; }
		public Action<PixelPoint>? PositionChanged { get; set; }
		public Action? Deactivated { get; set; }
		public Action? Activated { get; set; }
		public Action? GotInputWhenDisabled { get; set; }
		public Func<WindowCloseReason, bool>? Closing { get; set; }
		public Action<bool>? ExtendClientAreaToDecorationsChanged { get; set; }

		// --- les pixels ---------------------------------------------------------------------------

		internal Avalonia.OpenGL.Surfaces.IGlPlatformSurfaceRenderingSession BeginGl ()
		{
			if (_win == 0)
				throw new InvalidOperationException ("fenetre fermee");
			if (_gl == IntPtr.Zero)
				_gl = ArmpiPlatform.Gl!.TakeSurface (_win, _top);
			return ArmpiPlatform.Gl!.Begin (this, _gl, _win, _top, new PixelSize (_width, _height));
		}

		public IFramebufferRenderTarget CreateFramebufferRenderTarget () => new FuncFramebufferRenderTarget (Lock, true);

		ILockedFramebuffer Lock (IRenderTarget.RenderTargetSceneInfo sceneInfo, out FramebufferLockProperties properties)
		{
			properties = new FramebufferLockProperties (_retained);
			_retained = true;
			return new Locked (this);
		}

		sealed class Locked : ILockedFramebuffer
		{
			readonly ArmpiWindow _w;
			public Locked (ArmpiWindow w) => _w = w;
			public unsafe IntPtr Address => (IntPtr) _w._back;
			public PixelSize Size => new (_w._width, _w._height);
			public int RowBytes => (int) _w._pitch;
			public Vector Dpi => new (96, 96);
			public PixelFormat Format => PixelFormat.Bgra8888;
			public AlphaFormat AlphaFormat => AlphaFormat.Opaque;

			public unsafe void Dispose ()
			{
				if (_w._surface == 0)
					return;
				Buffer.MemoryCopy (_w._back, (void*) _w._pixels, _w._backSize, _w._backSize);
				UI.ui_surface_damage (_w._surface, 0, 0, 0, 0);
			}
		}

		// --- les evenements -----------------------------------------------------------------------

		internal void OnEvent (long type)
		{
			if (type == UI.WEV_MOUSE)
				OnMouse ();
			else if (type == UI.WEV_WHEEL)
				Send (new RawMouseWheelEventArgs (_mouse, Time, _root!, new Point (_mx, _my), new Vector (0, Ui.Event (UI.EV_Y)), Mods (Ui.Event (UI.EV_MODS), _buttons)));
			else if (type == UI.WEV_KEY)
				OnKey ();
			else if (type == UI.WEV_CLOSE)
			{
				if (Closing?.Invoke (WindowCloseReason.WindowClosing) != true)
					Dispose ();
			}
			else if (type == UI.WEV_FOCUS)
				OnFocus (Ui.Event (UI.EV_KIND) != 0);
			else if (type == UI.WEV_RESIZE)
				OnResize ();
		}

		void OnFocus (bool gained)
		{
			if (gained)
			{
				ZOrder = ++ArmpiPlatform.ZCounter;
				Activated?.Invoke ();
			}
			else
			{
				Deactivated?.Invoke ();
				LostFocus?.Invoke ();
			}
		}

		void OnResize ()
		{
			InWindow (() =>
			{
				UI.ui_w_event (0, out long _, out long _, out long _);		// (ui_resize, cadre, fond)
				_width = (int) Ui.Width;
				_height = Math.Max (1, (int) Ui.Height - _top);
			});
			PaintCaption ();
			AttachSurface ();
			Resized?.Invoke (new Size (_width, _height), WindowResizeReason.User);
		}

		void Send (RawInputEventArgs e)
		{
			if (_root == null)
				return;
			if (!_enabled)
			{
				GotInputWhenDisabled?.Invoke ();
				return;
			}
			Input?.Invoke (e);
		}

		static ulong Time => (ulong) Environment.TickCount64;

		static RawInputModifiers Mods (int mods, int buttons)
		{
			var m = ArmpiKeys.Modifiers (mods);
			if ((buttons & 1) != 0) m |= RawInputModifiers.LeftMouseButton;
			if ((buttons & 2) != 0) m |= RawInputModifiers.RightMouseButton;
			if ((buttons & 4) != 0) m |= RawInputModifiers.MiddleMouseButton;
			return m;
		}

		internal void OnMove (int x, int y, int buttons, int mods)
		{
			_mx = x;
			_my = y - _top;
			_buttons = buttons;
			Send (new RawPointerEventArgs (_mouse, Time, _root!, RawPointerEventType.Move, new Point (_mx, _my), Mods (mods, buttons)));
		}

		void OnMouse ()
		{
			int kind = Ui.Event (UI.EV_KIND), buttons = Ui.Event (UI.EV_CODE), mods = Ui.Event (UI.EV_MODS);
			int change = buttons ^ _buttons;
			if (kind == 0)
			{
				OnMove (Ui.Event (UI.EV_X), Ui.Event (UI.EV_Y), buttons, mods);
				return;
			}
			_mx = Ui.Event (UI.EV_X);
			_my = Ui.Event (UI.EV_Y) - _top;
			_buttons = buttons;
			if (change == 0)
				change = 1;
			RawPointerEventType t;
			if ((change & 1) != 0)
				t = kind == 1 ? RawPointerEventType.LeftButtonDown : RawPointerEventType.LeftButtonUp;
			else if ((change & 2) != 0)
				t = kind == 1 ? RawPointerEventType.RightButtonDown : RawPointerEventType.RightButtonUp;
			else
				t = kind == 1 ? RawPointerEventType.MiddleButtonDown : RawPointerEventType.MiddleButtonUp;
			Send (new RawPointerEventArgs (_mouse, Time, _root!, t, new Point (_mx, _my), Mods (mods, buttons)));
		}

		void OnKey ()
		{
			bool press = Ui.Event (UI.EV_KIND) != 0;
			int hid = Ui.Event (UI.EV_CODE), ch = Ui.Event (UI.EV_CHAR), mods = Ui.Event (UI.EV_MODS);
			var m = ArmpiKeys.Modifiers (mods) | Mods (0, _buttons);
			var key = ArmpiKeys.ToKey (hid, ch);
			var symbol = ch >= 32 && ch != 127 ? char.ConvertFromUtf32 (ch) : null;
			Send (new RawKeyEventArgs (_keyboard, Time, _root!, press ? RawKeyEventType.KeyDown : RawKeyEventType.KeyUp,
				key, m, ArmpiKeys.ToPhysical (hid), symbol));
			if (press && symbol != null && (m & (RawInputModifiers.Control | RawInputModifiers.Alt)) == 0)
				Send (new RawTextInputEventArgs (_keyboard, Time, _root!, symbol));
		}
	}
}
