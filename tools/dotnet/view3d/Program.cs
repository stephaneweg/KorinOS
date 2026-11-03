// view3d -- l'essai de System.3D dans une Form : la theiere ou les cubes qui tournent. Glisser : tourner autour
// de la scene ; molette : avancer. La ligne du bas dit qui dessine (GPU ou processeur) et en combien de temps.
//
//   view3d [--cubes] [--software] [--dry] [--bench images] [--out fichier]
//
// --bench lance les images aussi vite que possible, ecrit le bilan sur la console (ou le fichier de --out), puis
// quitte. --dry met
// GpuKit a blanc : gk_render construit ses listes sans rien envoyer et dit s'il accepte l'image, meme sans GPU
// (rien n'est dessine : c'est une verification, pas un mode d'affichage).
#nullable disable
using System;
using System.Diagnostics;
using System.Drawing;
using System.GpuKit;
using System.Windows.Forms;
using System.Windows.Media.Media3D;

class View3dForm : Form
{
	readonly Viewport3D _view = new () { Dock = DockStyle.Fill };
	readonly Label _status = new () { Dock = DockStyle.Fill, Text = "..." };
	readonly AxisAngleRotation3D _spin = new (new Vector3D (0, 1, 0), 30);
	readonly PerspectiveCamera _camera = new () { FieldOfView = 50, NearPlaneDistance = 0.5, FarPlaneDistance = 60 };
	readonly Timer _timer = new () { Interval = 16 };
	readonly Stopwatch _clock = Stopwatch.StartNew ();
	readonly int _bench;
	readonly string _out;
	ModelVisual3D _teapot, _cubes;
	bool _showCubes, _paused;
	double _yaw, _pitch = 18, _distance = 7.5, _lastSeconds;
	Point _drag;
	bool _dragging;
	int _frames, _measured;
	double _lastStatus, _build, _render, _gpuUs;
	long _benchStart;

	public View3dForm (bool cubes, bool software, int bench, string output)
	{
		_bench = bench;
		_out = output;
		_showCubes = cubes;
		Text = "Viewport3D";
		Size = new Size (640, 520);
		_view.Camera = _camera;
		_view.SoftwareOnly = software;

		var bar = new Panel { Dock = DockStyle.Bottom, Height = 36 };
		var scene = new Button { Text = "Scene", Dock = DockStyle.Left, Width = 80 };
		var pause = new Button { Text = "Pause", Dock = DockStyle.Left, Width = 80 };
		var cpu = new Button { Text = "GPU / CPU", Dock = DockStyle.Left, Width = 100 };
		scene.Click += (s, e) => { _showCubes = !_showCubes; ShowScene (); };
		pause.Click += (s, e) => _paused = !_paused;
		cpu.Click += (s, e) => _view.SoftwareOnly = !_view.SoftwareOnly;
		bar.Controls.AddRange (scene, pause, cpu, _status);
		Controls.AddRange (_view, bar);

		_view.MouseDown += (s, e) => { _dragging = true; _drag = e.Location; };
		_view.MouseUp += (s, e) => _dragging = false;
		_view.MouseMove += (s, e) =>
		{
			if (!_dragging)
				return;
			_yaw -= (e.X - _drag.X) * 0.5;
			_pitch = Math.Clamp (_pitch + (e.Y - _drag.Y) * 0.5, -85, 85);
			_drag = e.Location;
			PlaceCamera ();
		};
		_view.MouseWheel += (s, e) =>
		{
			_distance = Math.Clamp (_distance * (e.Delta > 0 ? 0.9 : 1.1), 0.8, 30);
			PlaceCamera ();
		};
		_view.Rendered += OnRendered;
		_timer.Tick += OnTick;
		if (bench > 0)
			_timer.Interval = 1;
		Shown += (s, e) => { ShowScene (); _timer.Start (); };
	}

	void ShowScene ()
	{
		_view.Children.Clear ();
		_view.Children.Add (_showCubes ? _cubes ??= Scenes.Cubes (_spin) : _teapot ??= Scenes.Teapot (_spin));
		PlaceCamera ();
	}

	void PlaceCamera ()
	{
		double y = _yaw * Math.PI / 180, p = _pitch * Math.PI / 180;
		var to = new Vector3D (Math.Cos (p) * Math.Sin (y), Math.Sin (p), Math.Cos (p) * Math.Cos (y)) * _distance;
		_camera.Position = new Point3D (to.X, to.Y, to.Z);
		_camera.LookDirection = -to;
	}

	void OnTick (object sender, EventArgs e)
	{
		double now = _clock.Elapsed.TotalSeconds, dt = now - _lastSeconds;
		_lastSeconds = now;
		if (!_paused)
			_spin.Angle = (_spin.Angle + (_bench > 0 ? 2 : dt * 50)) % 360;
	}

	void OnRendered (object sender, EventArgs e)
	{
		var s = _view.Stats;
		_frames++;
		if (_bench > 0)
		{
			if (_frames == _bench / 5)		// (le JIT d'abord)
				_benchStart = Stopwatch.GetTimestamp ();
			else if (_frames > _bench / 5)
				Measure (s);
			if (_frames == _bench)
				Report (s);
		}
		double now = _clock.Elapsed.TotalSeconds;
		if (now - _lastStatus >= 1)
		{
			_status.Text = (s.Gpu ? "GPU" : "CPU") + "  " + (_frames / (now - _lastStatus)).ToString ("0") + " i/s  scene "
				+ s.BuildMs.ToString ("0.0") + " + rendu " + s.RenderMs.ToString ("0.0") + " ms";
			_lastStatus = now;
			if (_bench == 0)
				_frames = 0;
		}
	}

	void Measure (FrameStats s)
	{
		_measured++;
		_build += s.BuildMs;
		_render += s.RenderMs;
		_gpuUs += s.GpuJobUs;
	}

	void Report (FrameStats s)
	{
		double all = (Stopwatch.GetTimestamp () - _benchStart) * 1000.0 / Stopwatch.Frequency;
		string text = "view3d : " + (s.Gpu ? "GPU" : "processeur (gk_render " + _view.GpuError + ")") + ", " + _view.Width + " x " + _view.Height + ", " + s.Triangles + " triangles, " + s.Batches + " lots, "
			+ s.Clipped + " decoupes\nview3d : " + _measured + " images, scene " + (_build / _measured).ToString ("0.00") + " ms, rendu " + (_render / _measured).ToString ("0.00")
			+ " ms, job GPU " + (_gpuUs / _measured / 1000).ToString ("0.00") + " ms, image complete " + (all / _measured).ToString ("0.00") + " ms\n";
		Console.Write (text);
		if (_out != null)
			System.IO.File.AppendAllText (_out, text);
		Application.Exit ();
	}
}

static class Program
{
	[STAThread]
	static void Main (string[] args)
	{
		int bench = 0;
		string output = null;
		bool cubes = false, software = false;
		for (int i = 0; i < args.Length; i++)
		{
			if (args[i] == "--cubes")
				cubes = true;
			else if (args[i] == "--software")
				software = true;
			else if (args[i] == "--dry")
				GK.gk_debug (GK.GK_DBG_DRY, 1, 0);
			else if (args[i] == "--bench" && i + 1 < args.Length)
				bench = int.Parse (args[++i]);
			else if (args[i] == "--out" && i + 1 < args.Length)
				output = args[++i];
		}
		Application.Run (new View3dForm (cubes, software, bench, output));
	}
}
