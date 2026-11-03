// ArmpiGl.cs -- Avalonia sur le GPU du Pi : Skia Ganesh sur Mesa, par armgl (docs/MESA.md), lie dans l'hote
// dotnet-skia. Un seul contexte, sur le thread de l'interface ou tourne aussi le rendu. Une surface par
// fenetre, de la taille de l'ecran : redimensionner ne change que la zone montree. Sans GPU (QEMU), ou avec
// AVALONIA_ARMPI_GPU=0, Graphics reste nul et le rendu reste dans le tampon de ArmpiWindow.
#nullable enable
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Avalonia.OpenGL;
using Avalonia.OpenGL.Surfaces;
using Avalonia.Platform;
using Avalonia.Reactive;

namespace Avalonia.Armpi
{
	internal sealed class ArmpiGlContext : IGlContext, IPlatformGraphics
	{
		const string Lib = "libarmgl";
		const int ArmglGpu = 1;
		[DllImport (Lib)] static extern int armgl_init (int flags);
		[DllImport (Lib)] static extern IntPtr armgl_context (int major, int minor, IntPtr share);
		[DllImport (Lib)] static extern IntPtr armgl_surface (int width, int height);
		[DllImport (Lib)] static extern IntPtr armgl_window (long win, int x, int y, int width, int height);
		[DllImport (Lib)] static extern void armgl_window_zone (IntPtr surface, long win, int x, int y, int width, int height);
		[DllImport (Lib)] static extern int armgl_make_current (IntPtr context, IntPtr surface);
		[DllImport (Lib)] static extern int armgl_swap (IntPtr surface);
		[DllImport (Lib, CharSet = CharSet.Ansi)] static extern IntPtr armgl_proc ([MarshalAs (UnmanagedType.LPStr)] string name);

		readonly IntPtr _context, _idle;
		readonly Stack<IntPtr> _free = new ();		// (le noyau garde les tampons : ils resservent)
		IntPtr _current;

		ArmpiGlContext (IntPtr context, IntPtr idle)
		{
			_context = context;
			_idle = idle;
			Current (idle);
			GlInterface = new GlInterface (Version, armgl_proc);
		}

		/// <summary>Le GPU, ou null : le rendu reste sur le processeur.</summary>
		internal static ArmpiGlContext? Create ()
		{
			if (Environment.GetEnvironmentVariable ("AVALONIA_ARMPI_GPU") == "0")
				return null;
			int state = armgl_init (0);
			Console.Error.WriteLine ("avalonia : rendu sur " + (state == ArmglGpu ? "le GPU" : "le processeur (armgl " + state + ")"));
			if (state != ArmglGpu)
				return null;
			IntPtr context = armgl_context (3, 0, IntPtr.Zero);
			IntPtr idle = armgl_surface (1, 1);
			return context == IntPtr.Zero || idle == IntPtr.Zero ? null : new ArmpiGlContext (context, idle);
		}

		void Current (IntPtr surface)
		{
			if (armgl_make_current (_context, surface) == 0)
				throw new InvalidOperationException ("armgl_make_current");
			_current = surface;
		}

		public GlVersion Version { get; } = new (GlProfileType.OpenGLES, 3, 0);
		public GlInterface GlInterface { get; }
		public int SampleCount => 0;
		public int StencilSize => 8;
		public bool IsLost => false;
		public bool CanCreateSharedContext => false;
		public bool IsSharedWith (IGlContext context) => context == this;
		public IGlContext? CreateSharedContext (IEnumerable<GlVersion>? preferredVersions = null) => null;
		public object? TryGetFeature (Type featureType) => null;
		public void Dispose () { }

		public IDisposable MakeCurrent ()
		{
			IntPtr was = _current;
			Current (_idle);
			return Disposable.Create (() => Current (was));
		}

		public IDisposable EnsureCurrent () => _current != IntPtr.Zero ? Disposable.Empty : MakeCurrent ();

		public bool UsesSharedContext => true;
		public IPlatformGraphicsContext CreateContext () => this;
		public IPlatformGraphicsContext GetSharedContext () => this;

		// --- les surfaces des fenetres -----------------------------------------------------------------

		internal IntPtr TakeSurface (long win, int top)
		{
			if (_free.TryPop (out IntPtr s))
				return s;
			var size = ArmpiScreens.Instance.Size;
			s = armgl_window (win, 0, top, size.Width, size.Height);
			if (s == IntPtr.Zero)
				throw new InvalidOperationException ("pas de surface GL pour la fenetre");
			return s;
		}

		internal void GiveBack (IntPtr surface)
		{
			if (_current == surface)
				Current (_idle);
			_free.Push (surface);
		}

		internal IGlPlatformSurfaceRenderingSession Begin (ArmpiWindow window, IntPtr surface, long win, int top, PixelSize size)
		{
			armgl_window_zone (surface, win, 0, top, size.Width, size.Height);
			IntPtr was = _current;
			Current (surface);
			return new Session (this, surface, was, size);
		}

		sealed class Session : IGlPlatformSurfaceRenderingSession
		{
			readonly ArmpiGlContext _gl;
			readonly IntPtr _surface, _was;

			public Session (ArmpiGlContext gl, IntPtr surface, IntPtr was, PixelSize size)
			{
				_gl = gl;
				_surface = surface;
				_was = was;
				Size = size;
			}

			public IGlContext Context => _gl;
			public PixelSize Size { get; }
			public double Scaling => 1;
			public bool IsYFlipped => false;

			public void Dispose ()
			{
				armgl_swap (_surface);
				_gl.Current (_was);
			}
		}
	}

	/// <summary>Le rendu d'une fenetre sur le GPU, a cote de son tampon (Surfaces).</summary>
	internal sealed class ArmpiGlSurface : IGlPlatformSurface
	{
		readonly ArmpiWindow _window;

		public ArmpiGlSurface (ArmpiWindow window) => _window = window;

		public IGlPlatformSurfaceRenderTarget CreateGlRenderTarget (IGlContext context) => new Target (_window);

		sealed class Target : IGlPlatformSurfaceRenderTarget
		{
			readonly ArmpiWindow _window;

			public Target (ArmpiWindow window) => _window = window;
			public void Dispose () { }
			public IGlPlatformSurfaceRenderingSession BeginDraw (IRenderTarget.RenderTargetSceneInfo sceneInfo) => _window.BeginGl ();
		}
	}
}
