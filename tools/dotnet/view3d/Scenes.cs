// Scenes.cs -- les scenes de view3d, partagees avec son essai sur le PC (tools/dotnet/view3dtest).
#nullable disable
using System.Drawing;
using System.Windows.Media.Media3D;

static class Scenes
{
	static Model3D Lights ()
	{
		var g = new Model3DGroup ();
		g.Children.Add (new AmbientLight (Color.FromArgb (44, 44, 52)));
		g.Children.Add (new DirectionalLight (Color.FromArgb (235, 232, 225), new Vector3D (-0.5, -0.8, -0.9)));
		g.Children.Add (new DirectionalLight (Color.FromArgb (60, 66, 90), new Vector3D (0.8, 0.2, -0.3)));
		return g;
	}

	/// <summary>Le GeometryModel3D tourne autour de y par spin.</summary>
	static GeometryModel3D Spinning (MeshGeometry3D mesh, Material material, AxisAngleRotation3D spin) =>
		new GeometryModel3D (mesh, material) { Transform = new RotateTransform3D (spin) };

	/// <summary>Cuivre, lumiere par sommet : 6400 triangles.</summary>
	public static ModelVisual3D Teapot (AxisAngleRotation3D spin)
	{
		var copper = new MaterialGroup ();
		copper.Children.Add (new DiffuseMaterial (Color.FromArgb (242, 140, 64)));
		copper.Children.Add (new SpecularMaterial (Color.FromArgb (150, 150, 150), 24));
		var g = new Model3DGroup ();
		g.Children.Add (Lights ());
		g.Children.Add (Spinning (global::Teapot.Build (), copper, spin));
		return new ModelVisual3D (g);
	}

	static ImageTexture Checker (uint a, uint b, int size, int cells)
	{
		var p = new uint[size * size];
		for (int y = 0; y < size; y++)
			for (int x = 0; x < size; x++)
				p[y * size + x] = (x * cells / size + y * cells / size) % 2 == 0 ? 0xFF000000 | a : 0xFF000000 | b;
		return new ImageTexture (p, size, size);
	}

	/// <summary>Tout le reste : texture, sphere, lumiere ponctuelle, faces arriere, verre, sol qui depasse la vue (decoupe).</summary>
	public static ModelVisual3D Cubes (AxisAngleRotation3D spin)
	{
		var g = new Model3DGroup ();
		g.Children.Add (Lights ());
		g.Children.Add (new PointLight (Color.FromArgb (190, 170, 120), new Point3D (0, 1.5, 2.5)) { LinearAttenuation = 0.2 });

		var floorTexture = Checker (0x8A92A6, 0x3C4254, 64, 2);
		var floor = new GeometryModel3D (MeshBuilder.Plane (40, 40, 20), new DiffuseMaterial (floorTexture));
		floor.Transform = new TranslateTransform3D (0, -1.4, 0);
		g.Children.Add (floor);

		var crate = new DiffuseMaterial (Checker (0xE8C468, 0xB0562C, 64, 8));
		g.Children.Add (Spinning (MeshBuilder.Box (1.6, 1.6, 1.6), crate, spin));

		var ball = new MaterialGroup ();
		ball.Children.Add (new DiffuseMaterial (Color.FromArgb (60, 120, 220)));
		ball.Children.Add (new SpecularMaterial (Color.White, 40));
		g.Children.Add (new GeometryModel3D (MeshBuilder.Sphere (0.8), ball) { Transform = new TranslateTransform3D (-2.3, -0.2, 0) });

		// un carton ouvert : ses deux faces se voient, chacune de sa couleur
		var card = new GeometryModel3D (MeshBuilder.Plane (1.4, 1.4), new DiffuseMaterial (Color.FromArgb (90, 190, 120)))
		{
			BackMaterial = new DiffuseMaterial (Color.FromArgb (200, 80, 90)),
		};
		var at = new Transform3DGroup ();
		at.Children.Add (new RotateTransform3D (new AxisAngleRotation3D (new Vector3D (1, 0, 0), 90)));
		at.Children.Add (new RotateTransform3D (spin));
		at.Children.Add (new TranslateTransform3D (2.3, -0.2, 0));
		card.Transform = at;
		g.Children.Add (card);

		// le verre, dessine en dernier
		var glass = new GeometryModel3D (MeshBuilder.Box (1.2, 2.2, 0.08), new DiffuseMaterial (Color.FromArgb (90, 150, 200, 255)))
		{
			Transform = new TranslateTransform3D (0.9, -0.1, 1.6),
		};
		g.Children.Add (glass);
		return new ModelVisual3D (g);
	}
}
