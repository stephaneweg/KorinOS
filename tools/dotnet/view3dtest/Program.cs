// view3dtest -- l'essai de System.3D sur le PC, sans fenetre ni GPU : Renderer3D par le processeur, images dans
// des .ppm, temps de chaque scene, et verifications qui ne dependent pas de l'oeil.
//     sh tools/dotnet/view3dtest.sh [dossier des images]
#nullable disable
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows.Media.Media3D;

static unsafe class Program
{
	const int W = 640, H = 480;
	static int s_fails;

	static uint[] Draw (string file, ModelVisual3D scene, Camera camera, out FrameStats stats, int runs = 1)
	{
		using var r = new Renderer3D { SoftwareOnly = true };
		var px = new uint[W * H];
		double best = 1e9;
		stats = default;
		fixed (uint* p = px)
		{
			for (int i = 0; i < runs; i++)
			{
				r.Render (camera, new Visual3D[] { scene }, p, W, H, W, 0x141822);
				stats = r.Stats;
				best = Math.Min (best, stats.BuildMs + stats.RenderMs);
			}
		}
		Console.WriteLine ($"{Path.GetFileName (file),-18} {stats.Triangles,6} tri {stats.Batches,3} lots {stats.Clipped,2} decoupes  scene {stats.BuildMs,6:0.00} ms  rendu {stats.RenderMs,6:0.00} ms");
		using (var f = File.Create (file))
		{
			f.Write (Encoding.ASCII.GetBytes ($"P6\n{W} {H}\n255\n"));
			foreach (uint c in px)
			{
				f.WriteByte ((byte) (c >> 16));
				f.WriteByte ((byte) (c >> 8));
				f.WriteByte ((byte) c);
			}
		}
		return px;
	}

	static void Check (bool ok, string what)
	{
		Console.WriteLine ((ok ? "  ok   " : "  FAUX ") + what);
		if (!ok)
			s_fails++;
	}

	static PerspectiveCamera At (double x, double y, double z) =>
		new PerspectiveCamera (new Point3D (x, y, z), new Vector3D (-x, -y, -z), new Vector3D (0, 1, 0), 50) { NearPlaneDistance = 0.5, FarPlaneDistance = 60 };

	static void Main (string[] args)
	{
		string dir = args.Length > 0 ? args[0] : ".";
		var spin = new AxisAngleRotation3D (new Vector3D (0, 1, 0), 30);
		var teapot = Scenes.Teapot (spin);
		var cubes = Scenes.Cubes (spin);

		var px = Draw (dir + "/teapot.ppm", teapot, At (0, 2.3, 7.2), out var s, 30);
		Check (s.Triangles == 6400 && s.Clipped == 0, "theiere : 6400 triangles, entiere dans la bande de garde");
		Check (px.Count (c => c != 0x141822) > 20000, "theiere : elle couvre l'image");

		// les normales sortent du corps, et chaque triangle tourne dans le sens de ses normales
		var m = Teapot.Build ();
		int outward = 0, body = 0, agree = 0;
		for (int i = 0; i < m.Positions.Count; i++)
		{
			var p = m.Positions[i];
			if (Math.Abs (p.Y) < 0.5 && Math.Abs (p.X) < 1 && Math.Abs (p.Z) > 1)	// (ni l'anse ni le bec : ils sont sur x)
			{
				body++;
				if (m.Normals[i].X * p.X + m.Normals[i].Z * p.Z > 0)
					outward++;
			}
		}
		for (int i = 0; i + 2 < m.TriangleIndices.Count; i += 3)
		{
			int a = m.TriangleIndices[i], b = m.TriangleIndices[i + 1], c = m.TriangleIndices[i + 2];
			var n = Vector3D.CrossProduct (m.Positions[b] - m.Positions[a], m.Positions[c] - m.Positions[a]);
			if (Vector3D.DotProduct (n, m.Normals[a] + m.Normals[b] + m.Normals[c]) >= 0)
				agree++;
		}
		Check (body > 0 && outward == body, "theiere : normales vers l'exterieur (" + outward + " / " + body + ")");
		Check (agree == m.TriangleIndices.Count / 3, "theiere : triangles dans le sens de leurs normales (" + agree + " / " + m.TriangleIndices.Count / 3 + ")");

		// vue de cote, la theiere bouche l'image
		var side = Draw (dir + "/teapot_side.ppm", teapot, At (7, 1, 0), out s);
		Check (side[(H / 2) * W + W / 2] != 0x141822, "theiere : pas de trou au centre");

		Draw (dir + "/cubes.ppm", cubes, At (0, 2.3, 7.2), out s, 10);
		Check (s.Clipped > 0, "cubes : le sol est decoupe");

		// la camera dans la theiere : tout est decoupe par le plan pres, sans sommet hors de la bande
		Draw (dir + "/teapot_in.ppm", teapot, At (0.5, 0.3, 0.8), out s);
		Check (s.Clipped > 0, "camera dans la theiere : decoupee");

		// camera orthographique, mesh sans normales (moyennees)
		var mesh = new MeshGeometry3D ();
		foreach (var p in new[] { new Point3D (-1, -1, 0), new Point3D (1, -1, 0), new Point3D (0, 1, 0) })
			mesh.Positions.Add (p);
		foreach (int k in new[] { 0, 1, 2 })
			mesh.TriangleIndices.Add (k);
		var g = new Model3DGroup ();
		g.Children.Add (new AmbientLight (System.Drawing.Color.White));
		g.Children.Add (new GeometryModel3D (mesh, new DiffuseMaterial (System.Drawing.Color.FromArgb (255, 0, 0))));
		var flat = new ModelVisual3D (g);
		var ortho = new OrthographicCamera { Position = new Point3D (0, 0, 5), LookDirection = new Vector3D (0, 0, -1), Width = 3 };
		var tri = Draw (dir + "/triangle.ppm", flat, ortho, out s);
		Check (tri[(H / 2) * W + W / 2] == 0xFF0000, "triangle rouge au centre : couleur exacte, sens direct vu de face");
		Check (tri[(H / 8) * W + W / 8] == 0x141822, "triangle : le coin reste au fond");
		ortho.Position = new Point3D (0, 0, -5);
		ortho.LookDirection = new Vector3D (0, 0, 1);
		tri = Draw (dir + "/triangle_back.ppm", flat, ortho, out s);
		Check (tri[(H / 2) * W + W / 2] == 0x141822, "triangle vu de dos : pas de BackMaterial, rien");

		Console.WriteLine (s_fails == 0 ? "tout est bon" : s_fails + " echecs");
		Environment.Exit (s_fails == 0 ? 0 : 1);
	}
}
