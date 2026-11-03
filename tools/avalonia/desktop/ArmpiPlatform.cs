// ArmpiPlatform.cs -- la plateforme de fenetres d'Avalonia sur Elegant, par UIKit, sans WinForms. Sur le
// modele d'Avalonia.LinuxFramebuffer et d'Avalonia.Headless : le rendu tourne sur le thread de l'interface,
// seul a parler a UIKit.
#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.UIKit;
using Avalonia.Controls.Platform;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Platform;
using Avalonia.Rendering;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;

namespace Avalonia.Armpi
{
	internal sealed class ArmpiWindowing : IWindowingPlatform
	{
		public IWindowImpl CreateWindow () => new ArmpiWindow ();
		public ITopLevelImpl CreateEmbeddableTopLevel () => throw new PlatformNotSupportedException ();
		public IWindowImpl CreateEmbeddableWindow () => throw new PlatformNotSupportedException ();
		public ITrayIconImpl? CreateTrayIcon () => null;

		public void GetWindowsZOrder (ReadOnlySpan<IWindowImpl> windows, Span<long> zOrder)
		{
			for (int i = 0; i < windows.Length; i++)
				zOrder[i] = (windows[i] as ArmpiWindow)?.ZOrder ?? 0;
		}
	}

	internal static class ArmpiPlatform
	{
		internal static Compositor Compositor { get; private set; } = null!;
		internal static ArmpiGlContext? Gl { get; private set; }
		internal static readonly Dictionary<long, ArmpiWindow> Windows = new ();
		internal static long ZCounter;

		internal static void Use (AppBuilder builder)
		{
			builder.UseStandardRuntimePlatformSubsystem ().UseWindowingSubsystem (Initialize, "armpi");
		}

		static void Initialize ()
		{
			Ui.Init ();
			Dispatcher.InitializeUIThreadDispatcher (new ArmpiDispatcher ());
			var clipboard = new ArmpiClipboard ();
			var fps = int.TryParse (Environment.GetEnvironmentVariable ("AVALONIA_ARMPI_FPS"), out int f) ? f : 60;
			AvaloniaLocator.CurrentMutable
				.Bind<IRenderLoop> ().ToConstant (RenderLoop.FromTimer (new UiThreadRenderTimer (fps)))
				.Bind<ICursorFactory> ().ToConstant (new ArmpiCursorFactory ())
				.Bind<IKeyboardDevice> ().ToConstant (new KeyboardDevice ())
				.Bind<IPlatformIconLoader> ().ToConstant (new ArmpiIconLoader ())
				.Bind<IPlatformSettings> ().ToConstant (new ArmpiSettings ())
				.Bind<PlatformHotkeyConfiguration> ().ToConstant (new PlatformHotkeyConfiguration (KeyModifiers.Control))
				.Bind<KeyGestureFormatInfo> ().ToConstant (new KeyGestureFormatInfo (new Dictionary<Key, string> ()))
				.Bind<IWindowingPlatform> ().ToConstant (new ArmpiWindowing ())
				.Bind<IClipboardImpl> ().ToConstant (clipboard)
				.Bind<IClipboard> ().ToConstant (new Clipboard (clipboard));
			Gl = ArmpiGlContext.Create ();
			Compositor = new Compositor (Gl);
		}

		/// <summary>Un evenement d'Elegant : ui_event vient de le rendre.</summary>
		internal static void Handle (long type)
		{
			if (Windows.TryGetValue (Ui.Event (UI.EV_WIN), out var w))
				w.OnEvent (type);
			else if (type == UI.WEV_SCREEN)
				ArmpiScreens.Instance.OnChanged ();
		}
	}

	/// <summary>
	/// La boucle : ui_event attend au plus Poll ms. EV_WAIT ne se reveille que par un evenement d'Elegant : ce
	/// qu'un autre thread demande (Signal) attend la fin de la tranche, comme dans la boucle de Jet.
	/// </summary>
	internal sealed class ArmpiDispatcher : IControlledDispatcherImpl, IDispatcherImplWithExplicitBackgroundProcessing
	{
		const int Poll = 10;
		readonly Thread _thread = Thread.CurrentThread;
		readonly Stopwatch _clock = Stopwatch.StartNew ();
		readonly object _lock = new ();
		bool _signaled, _background;
		long? _due;

		public bool CurrentThreadIsLoopThread => Thread.CurrentThread == _thread;
		public long Now => _clock.ElapsedMilliseconds;
		public bool CanQueryPendingInput => false;
		public bool HasPendingInput => false;
		public event Action? Signaled;
		public event Action? Timer;
		public event Action? ReadyForBackgroundProcessing;

		public void Signal () { lock (_lock) _signaled = true; }
		public void RequestBackgroundProcessing () { lock (_lock) _background = true; }
		public void UpdateTimer (long? dueTimeInMs) => _due = dueTimeInMs;

		public void RunLoop (CancellationToken token)
		{
			while (!token.IsCancellationRequested)
			{
				if (_due is long due && Now >= due)
				{
					_due = null;
					Timer?.Invoke ();
					continue;
				}
				bool signaled;
				lock (_lock)
				{
					signaled = _signaled;
					_signaled = false;
				}
				if (signaled)
				{
					Signaled?.Invoke ();
					continue;
				}
				if (Pump (0))
					continue;
				bool background;
				lock (_lock)
				{
					background = _background;
					_background = false;
				}
				if (background)
				{
					ReadyForBackgroundProcessing?.Invoke ();
					continue;
				}
				long wait = Poll;
				if (_due is long next)
					wait = Math.Clamp (next - Now, 1, Poll);
				Pump (wait);
			}
		}

		/// <summary>Les mouvements de souris en rafale n'en font qu'un ; true si un evenement a ete servi.</summary>
		static bool Pump (long wait)
		{
			long type = UI.ui_event (wait);
			if (type == 0)
				return false;
			ArmpiWindow? moved = null;
			int x = 0, y = 0, buttons = 0, mods = 0;
			while (type == UI.WEV_MOUSE && Ui.Event (UI.EV_KIND) == 0
				&& ArmpiPlatform.Windows.TryGetValue (Ui.Event (UI.EV_WIN), out var w))
			{
				moved = w;
				x = Ui.Event (UI.EV_X);
				y = Ui.Event (UI.EV_Y);
				buttons = Ui.Event (UI.EV_CODE);
				mods = Ui.Event (UI.EV_MODS);
				type = UI.ui_event (0);
			}
			moved?.OnMove (x, y, buttons, mods);
			if (type != 0)
				ArmpiPlatform.Handle (type);
			return true;
		}
	}
}
