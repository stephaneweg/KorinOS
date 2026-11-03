// Viewport3D.cs -- le controle : une scene 3D dans une Form. Il rend dans une surface attachee d'Elegant
// (ui_surface, docs/DESIGN-ELEGANT.md 17), que le compositeur montre sans recopie par la fenetre ; la vue
// d'UIKit en dessous ne sert qu'a recevoir la souris et le clavier. Une seule surface par fenetre, posee par
// Elegant au-dessus des widgets : un Viewport3D par fenetre, sans controle par-dessus.
#nullable disable
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
using System.UIKit;

namespace System.Windows.Media.Media3D;

public sealed unsafe class Viewport3D : Control
{
	Camera _camera;
	long _surface;
	bool _alive, _posted;
	readonly Renderer3D _renderer = new ();
	static readonly Dictionary<Control, Viewport3D> s_owners = new ();

	public Viewport3D ()
	{
		Children.Changed += SceneChanged;
		Resize += Placed;
		Size = new Size (320, 240);
	}

	public Camera Camera
	{
		get => _camera;
		set
		{
			if (_camera != null)
				_camera.Changed -= SceneChanged;
			_camera = value;
			if (value != null)
				value.Changed += SceneChanged;
			SceneChanged ();
		}
	}

	public Collection3D<Visual3D> Children { get; } = new ();

	/// <summary>Toujours le processeur, meme avec un GPU.</summary>
	public bool SoftwareOnly
	{
		get => _renderer.SoftwareOnly;
		set { _renderer.SoftwareOnly = value; SceneChanged (); }
	}

	/// <summary>Ce qu'a coute la derniere image.</summary>
	public FrameStats Stats => _renderer.Stats;

	/// <summary>Pourquoi le GPU n'a pas dessine (GK_E_* de gk_render) ; 0 s'il dessine.</summary>
	public long GpuError => _renderer.GpuError;

	/// <summary>Apres chaque image montree.</summary>
	public event EventHandler Rendered;

	/// <summary>Tous les changements d'un meme tour de boucle ne font qu'une image.</summary>
	void SceneChanged ()
	{
		if (_posted || _surface == 0)
			return;
		_posted = true;
		BeginInvoke (() =>
		{
			_posted = false;
			RenderNow ();
		});
	}

	/// <summary>L'image tout de suite, sans attendre la boucle.</summary>
	public void RenderNow ()
	{
		if (_surface == 0)
			return;
		UI.ui_surface_pixels (_surface, out long px, out long pitch, out long w, out long h);
		var back = BackColor.IsEmpty ? ThemeColors.Surface : BackColor;
		_renderer.Render (_camera, Children, (uint*) px, (int) w, (int) h, (int) (pitch / 4), (uint) back.ToArgb () & 0xFFFFFF);
		UI.ui_surface_damage (_surface, 0, 0, 0, 0);
		Rendered?.Invoke (this, EventArgs.Empty);
	}

	Control Window ()
	{
		Control c = this;
		while (c.Parent != null)
			c = c.Parent;
		return c;
	}

	void CreateSurface ()
	{
		if (_surface != 0 || !_alive || !Showing || Width < 1 || Height < 1)
			return;
		var win = Window ();
		if (s_owners.TryGetValue (win, out var other) && other != this)
			throw new InvalidOperationException ("Viewport3D : une seule surface par fenetre");
		var p = LocationInForm;
		_surface = UI.ui_surface (p.X, p.Y, Width, Height, UI.WS_SF_XRGB8888);
		if (_surface == 0)
			throw new InvalidOperationException ("Elegant refuse la surface de Viewport3D");
		s_owners[win] = this;
		SceneChanged ();
	}

	void FreeSurface ()
	{
		if (_surface != 0)
		{
			UI.ui_surface_free (_surface);
			s_owners.Remove (Window ());
		}
		_surface = 0;
	}

	protected override void OnHandleCreated (EventArgs e)
	{
		_alive = true;
		base.OnHandleCreated (e);
		CreateSurface ();
	}

	protected override void OnHandleDestroyed (EventArgs e)
	{
		_alive = false;
		FreeSurface ();
		base.OnHandleDestroyed (e);
	}

	protected override void OnVisibleChanged (EventArgs e)
	{
		base.OnVisibleChanged (e);
		if (Showing)
			CreateSurface ();
		else
			FreeSurface ();
	}

	/// <summary>La zone de la surface suit le controle ; un tampon neuf perd ses pixels, on redessine.</summary>
	void Placed (object sender, EventArgs e)
	{
		if (_surface == 0)
		{
			CreateSurface ();
			return;
		}
		if (Width < 1 || Height < 1)
			return;
		var p = LocationInForm;
		if (UI.ui_surface_move (_surface, p.X, p.Y, Width, Height) != 0)
			SceneChanged ();
	}

	protected override void OnPaint (PaintEventArgs e) => SceneChanged ();

	protected override void Dispose (bool disposing)
	{
		if (disposing)
			_renderer.Dispose ();
		base.Dispose (disposing);
	}
}
