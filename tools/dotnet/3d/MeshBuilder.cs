// MeshBuilder.cs -- les formes courantes en MeshGeometry3D : WPF n'en donne aucune, chaque programme les refait.
#nullable disable
using System;
using System.Drawing;

namespace System.Windows.Media.Media3D;

public static class MeshBuilder
{
	static void Quad (MeshGeometry3D m, Vector3D n, Vector3D u, Vector3D v, double hu, double hv, double dist)
	{
		int first = m.Positions.Count;
		var c = new Point3D (n.X * dist, n.Y * dist, n.Z * dist);
		m.Positions.Add (c - u * hu - v * hv);
		m.Positions.Add (c + u * hu - v * hv);
		m.Positions.Add (c + u * hu + v * hv);
		m.Positions.Add (c - u * hu + v * hv);
		m.TextureCoordinates.Add (new PointF (0, 1));
		m.TextureCoordinates.Add (new PointF (1, 1));
		m.TextureCoordinates.Add (new PointF (1, 0));
		m.TextureCoordinates.Add (new PointF (0, 0));
		for (int i = 0; i < 4; i++)
			m.Normals.Add (n);
		foreach (int k in new[] { 0, 1, 2, 0, 2, 3 })
			m.TriangleIndices.Add (first + k);
	}

	/// <summary>Une boite centree, une texture entiere par face.</summary>
	public static MeshGeometry3D Box (double width, double height, double depth)
	{
		var m = new MeshGeometry3D ();
		double hx = width / 2, hy = height / 2, hz = depth / 2;
		Vector3D x = new (1, 0, 0), y = new (0, 1, 0), z = new (0, 0, 1);
		Quad (m, z, x, y, hx, hy, hz);
		Quad (m, -z, -x, y, hx, hy, hz);
		Quad (m, x, -z, y, hz, hy, hx);
		Quad (m, -x, z, y, hz, hy, hx);
		Quad (m, y, x, -z, hx, hz, hy);
		Quad (m, -y, x, z, hx, hz, hy);
		return m;
	}

	/// <summary>Un plan horizontal (y = 0), centre, vu d'en haut, la texture repetee repeat fois.</summary>
	public static MeshGeometry3D Plane (double width, double depth, int repeat = 1)
	{
		var m = new MeshGeometry3D ();
		Quad (m, new Vector3D (0, 1, 0), new Vector3D (1, 0, 0), new Vector3D (0, 0, -1), width / 2, depth / 2, 0);
		for (int i = 0; i < 4; i++)
		{
			var t = m.TextureCoordinates[i];
			m.TextureCoordinates[i] = new PointF (t.X * repeat, t.Y * repeat);
		}
		return m;
	}

	/// <summary>Une sphere centree : slices meridiens, stacks paralleles.</summary>
	public static MeshGeometry3D Sphere (double radius, int slices = 32, int stacks = 16)
	{
		var m = new MeshGeometry3D ();
		for (int j = 0; j <= stacks; j++)
		{
			double phi = Math.PI * j / stacks, sp = Math.Sin (phi), cp = Math.Cos (phi);
			for (int i = 0; i <= slices; i++)
			{
				double a = 2 * Math.PI * i / slices;
				var n = new Vector3D (sp * Math.Sin (a), cp, sp * Math.Cos (a));
				m.Positions.Add (new Point3D (n.X * radius, n.Y * radius, n.Z * radius));
				m.Normals.Add (n);
				m.TextureCoordinates.Add (new PointF ((float) i / slices, (float) j / stacks));
			}
		}
		for (int j = 0; j < stacks; j++)
		{
			for (int i = 0; i < slices; i++)
			{
				int a = j * (slices + 1) + i, b = a + slices + 1;
				if (j != 0)
				{
					m.TriangleIndices.Add (a);
					m.TriangleIndices.Add (b);
					m.TriangleIndices.Add (a + 1);
				}
				if (j != stacks - 1)
				{
					m.TriangleIndices.Add (a + 1);
					m.TriangleIndices.Add (b);
					m.TriangleIndices.Add (b + 1);
				}
			}
		}
		return m;
	}
}
