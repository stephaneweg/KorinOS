// Teapot.cs -- la theiere de l'Utah (Martin Newell, 1975) en MeshGeometry3D : ses carreaux de Bezier, de teapot.h
// d'Onyx, 6400 triangles. Donnees : fg_teapot_data.h de freeglut (licence X11/MIT, (c) Pawel W. Olszta et les
// contributeurs de freeglut). Les carreaux 0-5 (bord, corps, couvercle, fond) sont copies dans les quatre
// quadrants, les 6-9 (anse, bec) en miroir selon y.
#nullable disable
using System;
using System.Windows.Media.Media3D;

static class Teapot
{
	const int N = 10;			// segments par cote de carreau

	static readonly int[][] Patches =
	{
		new[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		new[] { 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 },
		new[] { 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 },
		new[] { 40, 41, 42, 40, 43, 44, 45, 46, 47, 47, 47, 47, 48, 49, 50, 51 },
		new[] { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 },
		new[] { 64, 64, 64, 64, 65, 66, 67, 68, 69, 70, 71, 72, 39, 38, 37, 36 },
		new[] { 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88 },
		new[] { 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100 },
		new[] { 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116 },
		new[] { 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128 },
	};

	static readonly float[] Control =
	{
		1.40000f, 0.00000f, 2.40000f, 1.40000f, -0.78400f, 2.40000f, 0.78400f, -1.40000f, 2.40000f,
		0.00000f, -1.40000f, 2.40000f, 1.33750f, 0.00000f, 2.53125f, 1.33750f, -0.74900f, 2.53125f,
		0.74900f, -1.33750f, 2.53125f, 0.00000f, -1.33750f, 2.53125f, 1.43750f, 0.00000f, 2.53125f,
		1.43750f, -0.80500f, 2.53125f, 0.80500f, -1.43750f, 2.53125f, 0.00000f, -1.43750f, 2.53125f,
		1.50000f, 0.00000f, 2.40000f, 1.50000f, -0.84000f, 2.40000f, 0.84000f, -1.50000f, 2.40000f,
		0.00000f, -1.50000f, 2.40000f, 1.75000f, 0.00000f, 1.87500f, 1.75000f, -0.98000f, 1.87500f,
		0.98000f, -1.75000f, 1.87500f, 0.00000f, -1.75000f, 1.87500f, 2.00000f, 0.00000f, 1.35000f,
		2.00000f, -1.12000f, 1.35000f, 1.12000f, -2.00000f, 1.35000f, 0.00000f, -2.00000f, 1.35000f,
		2.00000f, 0.00000f, 0.90000f, 2.00000f, -1.12000f, 0.90000f, 1.12000f, -2.00000f, 0.90000f,
		0.00000f, -2.00000f, 0.90000f, 2.00000f, 0.00000f, 0.45000f, 2.00000f, -1.12000f, 0.45000f,
		1.12000f, -2.00000f, 0.45000f, 0.00000f, -2.00000f, 0.45000f, 1.50000f, 0.00000f, 0.22500f,
		1.50000f, -0.84000f, 0.22500f, 0.84000f, -1.50000f, 0.22500f, 0.00000f, -1.50000f, 0.22500f,
		1.50000f, 0.00000f, 0.15000f, 1.50000f, -0.84000f, 0.15000f, 0.84000f, -1.50000f, 0.15000f,
		0.00000f, -1.50000f, 0.15000f, 0.00000f, 0.00000f, 3.15000f, 0.00000f, -0.00200f, 3.15000f,
		0.00200f, 0.00000f, 3.15000f, 0.80000f, 0.00000f, 3.15000f, 0.80000f, -0.45000f, 3.15000f,
		0.45000f, -0.80000f, 3.15000f, 0.00000f, -0.80000f, 3.15000f, 0.00000f, 0.00000f, 2.85000f,
		0.20000f, 0.00000f, 2.70000f, 0.20000f, -0.11200f, 2.70000f, 0.11200f, -0.20000f, 2.70000f,
		0.00000f, -0.20000f, 2.70000f, 0.40000f, 0.00000f, 2.55000f, 0.40000f, -0.22400f, 2.55000f,
		0.22400f, -0.40000f, 2.55000f, 0.00000f, -0.40000f, 2.55000f, 1.30000f, 0.00000f, 2.55000f,
		1.30000f, -0.72800f, 2.55000f, 0.72800f, -1.30000f, 2.55000f, 0.00000f, -1.30000f, 2.55000f,
		1.30000f, 0.00000f, 2.40000f, 1.30000f, -0.72800f, 2.40000f, 0.72800f, -1.30000f, 2.40000f,
		0.00000f, -1.30000f, 2.40000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, -1.42500f, 0.00000f,
		0.79800f, -1.42500f, 0.00000f, 1.42500f, -0.79800f, 0.00000f, 1.42500f, 0.00000f, 0.00000f,
		0.00000f, -1.50000f, 0.07500f, 0.84000f, -1.50000f, 0.07500f, 1.50000f, -0.84000f, 0.07500f,
		1.50000f, 0.00000f, 0.07500f, -1.60000f, 0.00000f, 2.02500f, -1.60000f, -0.30000f, 2.02500f,
		-1.50000f, -0.30000f, 2.25000f, -1.50000f, 0.00000f, 2.25000f, -2.30000f, 0.00000f, 2.02500f,
		-2.30000f, -0.30000f, 2.02500f, -2.50000f, -0.30000f, 2.25000f, -2.50000f, 0.00000f, 2.25000f,
		-2.70000f, 0.00000f, 2.02500f, -2.70000f, -0.30000f, 2.02500f, -3.00000f, -0.30000f, 2.25000f,
		-3.00000f, 0.00000f, 2.25000f, -2.70000f, 0.00000f, 1.80000f, -2.70000f, -0.30000f, 1.80000f,
		-3.00000f, -0.30000f, 1.80000f, -3.00000f, 0.00000f, 1.80000f, -2.70000f, 0.00000f, 1.57500f,
		-2.70000f, -0.30000f, 1.57500f, -3.00000f, -0.30000f, 1.35000f, -3.00000f, 0.00000f, 1.35000f,
		-2.50000f, 0.00000f, 1.12500f, -2.50000f, -0.30000f, 1.12500f, -2.65000f, -0.30000f, 0.93750f,
		-2.65000f, 0.00000f, 0.93750f, -2.00000f, 0.00000f, 0.90000f, -2.00000f, -0.30000f, 0.90000f,
		-1.90000f, -0.30000f, 0.60000f, -1.90000f, 0.00000f, 0.60000f, 1.70000f, 0.00000f, 1.42500f,
		1.70000f, -0.66000f, 1.42500f, 1.70000f, -0.66000f, 0.60000f, 1.70000f, 0.00000f, 0.60000f,
		2.60000f, 0.00000f, 1.42500f, 2.60000f, -0.66000f, 1.42500f, 3.10000f, -0.66000f, 0.82500f,
		3.10000f, 0.00000f, 0.82500f, 2.30000f, 0.00000f, 2.10000f, 2.30000f, -0.25000f, 2.10000f,
		2.40000f, -0.25000f, 2.02500f, 2.40000f, 0.00000f, 2.02500f, 2.70000f, 0.00000f, 2.40000f,
		2.70000f, -0.25000f, 2.40000f, 3.30000f, -0.25000f, 2.40000f, 3.30000f, 0.00000f, 2.40000f,
		2.80000f, 0.00000f, 2.47500f, 2.80000f, -0.25000f, 2.47500f, 3.52500f, -0.25000f, 2.49375f,
		3.52500f, 0.00000f, 2.49375f, 2.90000f, 0.00000f, 2.47500f, 2.90000f, -0.15000f, 2.47500f,
		3.45000f, -0.15000f, 2.51250f, 3.45000f, 0.00000f, 2.51250f, 2.80000f, 0.00000f, 2.40000f,
		2.80000f, -0.15000f, 2.40000f, 3.20000f, -0.15000f, 2.40000f, 3.20000f, 0.00000f, 2.40000f,
	};

	static void Bernstein (double t, double[] b, double[] d)
	{
		double s = 1 - t;
		b[0] = s * s * s; b[1] = 3 * t * s * s; b[2] = 3 * t * t * s; b[3] = t * t * t;
		d[0] = -3 * s * s; d[1] = 3 * s * s - 6 * t * s; d[2] = 6 * t * s - 3 * t * t; d[3] = 3 * t * t;
	}

	static void Eval (Vector3D[] cp, double u, double v, out Vector3D p, out Vector3D du, out Vector3D dv)
	{
		double[] bu = new double[4], su = new double[4], bv = new double[4], sv = new double[4];
		Bernstein (u, bu, su);
		Bernstein (v, bv, sv);
		p = du = dv = default;
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				var c = cp[i * 4 + j];
				p += c * (bu[i] * bv[j]);
				du += c * (su[i] * bv[j]);
				dv += c * (bu[i] * sv[j]);
			}
		}
	}

	/// <summary>Y vers le haut, centree, de 3,15 de haut.</summary>
	public static MeshGeometry3D Build ()
	{
		var mesh = new MeshGeometry3D ();
		double[][] quadrants = { new double[] { 1, 1 }, new double[] { -1, 1 }, new double[] { -1, -1 }, new double[] { 1, -1 } };
		double[][] mirrors = { new double[] { 1, 1 }, new double[] { 1, -1 } };
		for (int ip = 0; ip < Patches.Length; ip++)
		{
			foreach (var q in ip < 6 ? quadrants : mirrors)
			{
				bool flipped = q[0] * q[1] > 0;		// les donnees regardent vers l'interieur, le miroir vers l'exterieur
				var cp = new Vector3D[16];
				for (int i = 0; i < 16; i++)
				{
					int k = Patches[ip][i] * 3;
					cp[i] = new Vector3D (Control[k] * q[0], Control[k + 1] * q[1], Control[k + 2]);
				}
				int first = mesh.Positions.Count;
				for (int i = 0; i <= N; i++)
				{
					for (int j = 0; j <= N; j++)
					{
						double u = (double) i / N, v = (double) j / N;
						Eval (cp, u, v, out var p, out var du, out var dv);
						var n = Vector3D.CrossProduct (du, dv);
						if (Vector3D.DotProduct (n, n) < 1e-10)	// un pole (bouton du couvercle, fond)
						{
							Eval (cp, u < 0.5 ? u + 0.01 : u - 0.01, v < 0.5 ? v + 0.01 : v - 0.01, out _, out du, out dv);
							n = Vector3D.CrossProduct (du, dv);
						}
						if (flipped)
							n = -n;
						n.Normalize ();
						mesh.Positions.Add (new Point3D (p.X, p.Z - 1.55, -p.Y));
						mesh.Normals.Add (new Vector3D (n.X, n.Z, -n.Y));
					}
				}
				for (int i = 0; i < N; i++)
				{
					for (int j = 0; j < N; j++)
					{
						int a = first + i * (N + 1) + j, b = a + 1, c = a + N + 1, d = c + 1;
						foreach (int k in flipped ? new[] { a, b, c, b, d, c } : new[] { a, c, b, b, c, d })
							mesh.TriangleIndices.Add (k);
					}
				}
			}
		}
		return mesh;
	}
}
