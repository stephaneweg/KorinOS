// Scene.cs -- le graphe de scene de System.3D, retenu comme celui de WPF : camera, visuels, modeles,
// geometrie, materiaux, lumieres, transformations. Tout objet signale ses changements a ce qui le contient,
// jusqu'au Viewport3D, qui redessine une fois : un programme n'a qu'a changer une valeur.
// Noms de WPF, mais une couleur est une System.Drawing.Color et une image une ImageTexture (pas de Brush).
#nullable disable
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Drawing;
using System.Runtime.InteropServices;
using System.UIKit;

namespace System.Windows.Media.Media3D;

/// <summary>Ce qui signale ses changements a son contenant.</summary>
public abstract class Media3DObject
{
	internal event Action Changed;
	internal void NotifyChanged () => Changed?.Invoke ();

	/// <summary>Pose une valeur qui signale elle-meme ses changements.</summary>
	internal void Attach<T> (ref T field, T value) where T : Media3DObject
	{
		if (field != null)
			field.Changed -= NotifyChanged;
		field = value;
		if (value != null)
			value.Changed += NotifyChanged;
		NotifyChanged ();
	}

	internal void Set<T> (ref T field, T value)
	{
		field = value;
		NotifyChanged ();
	}
}

/// <summary>Une liste dont tout changement se voit, celui des objets qu'elle contient aussi.</summary>
public class Collection3D<T> : Collection<T>
{
	internal event Action Changed;
	void Fire () => Changed?.Invoke ();
	void Hook (T item, bool on)
	{
		if (item is Media3DObject o)
		{
			if (on)
				o.Changed += Fire;
			else
				o.Changed -= Fire;
		}
	}
	protected override void InsertItem (int index, T item) { base.InsertItem (index, item); Hook (item, true); Fire (); }
	protected override void SetItem (int index, T item) { Hook (this[index], false); base.SetItem (index, item); Hook (item, true); Fire (); }
	protected override void RemoveItem (int index) { Hook (this[index], false); base.RemoveItem (index); Fire (); }
	protected override void ClearItems ()
	{
		foreach (var i in this)
			Hook (i, false);
		base.ClearItems ();
		Fire ();
	}
}

// ---- transformations --------------------------------------------------------------------------------------

public abstract class Transform3D : Media3DObject
{
	public abstract Matrix3D Value { get; }
	public static Transform3D Identity => new MatrixTransform3D (Matrix3D.Identity);
}

public sealed class MatrixTransform3D : Transform3D
{
	Matrix3D _m;
	public MatrixTransform3D () { _m = Matrix3D.Identity; }
	public MatrixTransform3D (Matrix3D m) { _m = m; }
	public Matrix3D Matrix { get => _m; set => Set (ref _m, value); }
	public override Matrix3D Value => _m;
}

public sealed class TranslateTransform3D : Transform3D
{
	double _x, _y, _z;
	public TranslateTransform3D () { }
	public TranslateTransform3D (double x, double y, double z) { _x = x; _y = y; _z = z; }
	public double OffsetX { get => _x; set => Set (ref _x, value); }
	public double OffsetY { get => _y; set => Set (ref _y, value); }
	public double OffsetZ { get => _z; set => Set (ref _z, value); }
	public override Matrix3D Value => Matrix3D.Translation (_x, _y, _z);
}

public sealed class ScaleTransform3D : Transform3D
{
	double _x = 1, _y = 1, _z = 1;
	public ScaleTransform3D () { }
	public ScaleTransform3D (double x, double y, double z) { _x = x; _y = y; _z = z; }
	public double ScaleX { get => _x; set => Set (ref _x, value); }
	public double ScaleY { get => _y; set => Set (ref _y, value); }
	public double ScaleZ { get => _z; set => Set (ref _z, value); }
	public override Matrix3D Value => Matrix3D.Scaling (_x, _y, _z);
}

public sealed class AxisAngleRotation3D : Media3DObject
{
	Vector3D _axis = new Vector3D (0, 1, 0);
	double _angle;
	public AxisAngleRotation3D () { }
	public AxisAngleRotation3D (Vector3D axis, double angle) { _axis = axis; _angle = angle; }
	public Vector3D Axis { get => _axis; set => Set (ref _axis, value); }
	public double Angle { get => _angle; set => Set (ref _angle, value); }
}

public sealed class RotateTransform3D : Transform3D
{
	AxisAngleRotation3D _r;
	double _cx, _cy, _cz;
	public RotateTransform3D () { Rotation = new AxisAngleRotation3D (); }
	public RotateTransform3D (AxisAngleRotation3D rotation) { Rotation = rotation; }
	public AxisAngleRotation3D Rotation { get => _r; set => Attach (ref _r, value); }
	public double CenterX { get => _cx; set => Set (ref _cx, value); }
	public double CenterY { get => _cy; set => Set (ref _cy, value); }
	public double CenterZ { get => _cz; set => Set (ref _cz, value); }
	public override Matrix3D Value
	{
		get
		{
			var m = Matrix3D.Rotation (_r.Axis, _r.Angle);
			if (_cx != 0 || _cy != 0 || _cz != 0)
				m = Matrix3D.Translation (-_cx, -_cy, -_cz) * m * Matrix3D.Translation (_cx, _cy, _cz);
			return m;
		}
	}
}

public sealed class Transform3DGroup : Transform3D
{
	public Collection3D<Transform3D> Children { get; } = new ();
	public Transform3DGroup () { Children.Changed += NotifyChanged; }
	public override Matrix3D Value
	{
		get
		{
			var m = Matrix3D.Identity;
			foreach (var t in Children)
				m = m * t.Value;
			return m;
		}
	}
}

// ---- cameras -----------------------------------------------------------------------------------------------

public abstract class Camera : Media3DObject
{
	Point3D _position = new Point3D (0, 0, 2);
	Vector3D _look = new Vector3D (0, 0, -1), _up = new Vector3D (0, 1, 0);
	double _near = 0.125, _far = 100;
	public Point3D Position { get => _position; set => Set (ref _position, value); }
	public Vector3D LookDirection { get => _look; set => Set (ref _look, value); }
	public Vector3D UpDirection { get => _up; set => Set (ref _up, value); }
	public double NearPlaneDistance { get => _near; set => Set (ref _near, value); }
	public double FarPlaneDistance { get => _far; set => Set (ref _far, value); }

	/// <summary>Monde -> repere de la camera : x a droite, y en haut, elle regarde vers -z.</summary>
	internal Matrix3D View ()
	{
		var z = -_look;
		z.Normalize ();
		var x = Vector3D.CrossProduct (_up, z);
		x.Normalize ();
		var y = Vector3D.CrossProduct (z, x);
		var o = new Vector3D (_position.X, _position.Y, _position.Z);
		return new Matrix3D (x.X, y.X, z.X, 0, x.Y, y.Y, z.Y, 0, x.Z, y.Z, z.Z, 0,
			-Vector3D.DotProduct (x, o), -Vector3D.DotProduct (y, o), -Vector3D.DotProduct (z, o), 1);
	}

	/// <summary>Repere de la camera -> espace de decoupe, z de -1 (pres) a 1 (loin) comme OpenGL et GpuKit.</summary>
	internal abstract Matrix3D Projection (double aspect);
}

public sealed class PerspectiveCamera : Camera
{
	double _fov = 45;
	public PerspectiveCamera () { }
	public PerspectiveCamera (Point3D position, Vector3D look, Vector3D up, double fieldOfView)
	{
		Position = position;
		LookDirection = look;
		UpDirection = up;
		_fov = fieldOfView;
	}
	/// <summary>L'angle de vue horizontal, en degres (comme WPF).</summary>
	public double FieldOfView { get => _fov; set => Set (ref _fov, value); }

	internal override Matrix3D Projection (double aspect)
	{
		double fx = 1 / Math.Tan (_fov * Math.PI / 360), fy = fx * aspect, n = NearPlaneDistance, f = FarPlaneDistance;
		return new Matrix3D (fx, 0, 0, 0, 0, fy, 0, 0, 0, 0, (f + n) / (n - f), -1, 0, 0, 2 * f * n / (n - f), 0);
	}
}

public sealed class OrthographicCamera : Camera
{
	double _width = 2;
	public OrthographicCamera () { }
	/// <summary>La largeur vue, en unites de la scene.</summary>
	public double Width { get => _width; set => Set (ref _width, value); }

	internal override Matrix3D Projection (double aspect)
	{
		double sx = 2 / _width, sy = sx * aspect, n = NearPlaneDistance, f = FarPlaneDistance;
		return new Matrix3D (sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 2 / (n - f), 0, 0, 0, (f + n) / (n - f), 1);
	}
}

// ---- geometrie ---------------------------------------------------------------------------------------------

public abstract class Geometry3D : Media3DObject { }

/// <summary>Triangles indexes, dans le sens direct vu de l'exterieur. Sans Normals, elles sont moyennees.</summary>
public sealed class MeshGeometry3D : Geometry3D
{
	public Collection3D<Point3D> Positions { get; } = new ();
	public Collection3D<Vector3D> Normals { get; } = new ();
	public Collection3D<PointF> TextureCoordinates { get; } = new ();
	public Collection3D<int> TriangleIndices { get; } = new ();
	MeshData _data;

	public MeshGeometry3D ()
	{
		Action a = () => { _data = null; NotifyChanged (); };
		Positions.Changed += a;
		Normals.Changed += a;
		TextureCoordinates.Changed += a;
		TriangleIndices.Changed += a;
	}

	/// <summary>En float, une fois, pour le rendu.</summary>
	internal MeshData Data => _data ??= new MeshData (this);
}

internal sealed class MeshData
{
	internal readonly float[] P, N, UV;	// x y z, x y z, s t par sommet
	internal readonly int[] Index;
	internal readonly int Vertices;
	internal readonly float MinX, MinY, MinZ, MaxX, MaxY, MaxZ;

	internal MeshData (MeshGeometry3D m)
	{
		Vertices = m.Positions.Count;
		P = new float[Vertices * 3];
		N = new float[Vertices * 3];
		UV = new float[Vertices * 2];
		Index = new int[m.TriangleIndices.Count / 3 * 3];
		for (int i = 0; i < Index.Length; i++)
		{
			Index[i] = m.TriangleIndices[i];
			if (Index[i] < 0 || Index[i] >= Vertices)
				throw new InvalidOperationException ("TriangleIndices : sommet " + Index[i] + " inconnu");
		}
		MinX = MinY = MinZ = float.MaxValue;
		MaxX = MaxY = MaxZ = float.MinValue;
		for (int i = 0; i < Vertices; i++)
		{
			var p = m.Positions[i];
			float x = (float) p.X, y = (float) p.Y, z = (float) p.Z;
			P[i * 3] = x;
			P[i * 3 + 1] = y;
			P[i * 3 + 2] = z;
			MinX = Math.Min (MinX, x); MinY = Math.Min (MinY, y); MinZ = Math.Min (MinZ, z);
			MaxX = Math.Max (MaxX, x); MaxY = Math.Max (MaxY, y); MaxZ = Math.Max (MaxZ, z);
			if (i < m.TextureCoordinates.Count)
			{
				UV[i * 2] = m.TextureCoordinates[i].X;
				UV[i * 2 + 1] = m.TextureCoordinates[i].Y;
			}
		}
		if (m.Normals.Count == Vertices)
		{
			for (int i = 0; i < Vertices; i++)
			{
				var v = m.Normals[i];
				v.Normalize ();
				N[i * 3] = (float) v.X;
				N[i * 3 + 1] = (float) v.Y;
				N[i * 3 + 2] = (float) v.Z;
			}
			return;
		}
		var sum = new Vector3D[Vertices];	// (la surface du triangle pese)
		for (int i = 0; i + 2 < Index.Length; i += 3)
		{
			int a = Index[i], b = Index[i + 1], c = Index[i + 2];
			var n = Vector3D.CrossProduct (m.Positions[b] - m.Positions[a], m.Positions[c] - m.Positions[a]);
			sum[a] += n;
			sum[b] += n;
			sum[c] += n;
		}
		for (int i = 0; i < Vertices; i++)
		{
			sum[i].Normalize ();
			N[i * 3] = (float) sum[i].X;
			N[i * 3 + 1] = (float) sum[i].Y;
			N[i * 3 + 2] = (float) sum[i].Z;
		}
	}
}

// ---- materiaux ---------------------------------------------------------------------------------------------

/// <summary>Une image de la scene, en 0xAARRGGBB. Repetee, sans mipmaps (GpuKit n'en fait pas).</summary>
public sealed class ImageTexture
{
	internal readonly uint[] Pixels;
	internal readonly int Width, Height;
	internal int GpuId = -1;
	public bool Linear { get; set; } = true;

	public ImageTexture (uint[] pixels, int width, int height)
	{
		if (width < 1 || height < 1 || width > 2048 || height > 2048 || pixels.Length < width * height)
			throw new ArgumentException ("texture : de 1 a 2048 pixels de cote");
		Pixels = pixels;
		Width = width;
		Height = height;
	}

	public static unsafe ImageTexture FromFile (string path)
	{
		long h = Ui.With (path, p => UI.ui_image_load (p));
		if (h == 0)
			throw new System.IO.FileNotFoundException ("image illisible", path);
		UI.ui_image_size (h, out long w, out long ht, out long px);
		var a = new uint[w * ht];
		new ReadOnlySpan<uint> ((void*) px, a.Length).CopyTo (a);
		UI.ui_image_free (h);
		return new ImageTexture (a, (int) w, (int) ht);
	}
}

public abstract class Material : Media3DObject
{
	internal abstract void Add (ref MaterialData d);
}

/// <summary>Ce que le rendu retient d'un materiau : couleurs de 0 a 1.</summary>
internal struct MaterialData
{
	internal float R, G, B, A;		// diffus
	internal float SR, SG, SB, Power;	// speculaire
	internal float ER, EG, EB;		// emis
	internal bool Diffuse;
	internal ImageTexture Texture;
}

public sealed class DiffuseMaterial : Material
{
	Color _color = Color.White;
	ImageTexture _texture;
	public DiffuseMaterial () { }
	public DiffuseMaterial (Color color) { _color = color; }
	public DiffuseMaterial (ImageTexture texture) { _texture = texture; }
	public Color Color { get => _color; set => Set (ref _color, value); }
	/// <summary>Multipliee par la lumiere et la couleur.</summary>
	public ImageTexture Texture { get => _texture; set => Set (ref _texture, value); }
	internal override void Add (ref MaterialData d)
	{
		d.R = _color.R / 255f;
		d.G = _color.G / 255f;
		d.B = _color.B / 255f;
		d.A = _color.A / 255f;
		d.Diffuse = true;
		d.Texture = _texture;
	}
}

public sealed class SpecularMaterial : Material
{
	Color _color = Color.White;
	double _power = 32;
	public SpecularMaterial () { }
	public SpecularMaterial (Color color, double power) { _color = color; _power = power; }
	public Color Color { get => _color; set => Set (ref _color, value); }
	public double SpecularPower { get => _power; set => Set (ref _power, value); }
	internal override void Add (ref MaterialData d)
	{
		d.SR = _color.R / 255f;
		d.SG = _color.G / 255f;
		d.SB = _color.B / 255f;
		d.Power = (float) _power;
	}
}

public sealed class EmissiveMaterial : Material
{
	Color _color = Color.White;
	public EmissiveMaterial () { }
	public EmissiveMaterial (Color color) { _color = color; }
	public Color Color { get => _color; set => Set (ref _color, value); }
	internal override void Add (ref MaterialData d)
	{
		d.ER = _color.R / 255f;
		d.EG = _color.G / 255f;
		d.EB = _color.B / 255f;
	}
}

public sealed class MaterialGroup : Material
{
	public Collection3D<Material> Children { get; } = new ();
	public MaterialGroup () { Children.Changed += NotifyChanged; }
	internal override void Add (ref MaterialData d)
	{
		foreach (var m in Children)
			m.Add (ref d);
	}
}

// ---- modeles -----------------------------------------------------------------------------------------------

public abstract class Model3D : Media3DObject
{
	Transform3D _transform;
	public Transform3D Transform { get => _transform; set => Attach (ref _transform, value); }
}

public sealed class Model3DGroup : Model3D
{
	public Collection3D<Model3D> Children { get; } = new ();
	public Model3DGroup () { Children.Changed += NotifyChanged; }
}

public sealed class GeometryModel3D : Model3D
{
	Geometry3D _geometry;
	Material _material, _back;
	public GeometryModel3D () { }
	public GeometryModel3D (Geometry3D geometry, Material material) { Geometry = geometry; Material = material; }
	public Geometry3D Geometry { get => _geometry; set => Attach (ref _geometry, value); }
	public Material Material { get => _material; set => Attach (ref _material, value); }
	/// <summary>Les faces arriere ; sans lui, elles ne sont pas dessinees.</summary>
	public Material BackMaterial { get => _back; set => Attach (ref _back, value); }
}

public abstract class Light : Model3D
{
	Color _color = Color.White;
	public Color Color { get => _color; set => Set (ref _color, value); }
}

public sealed class AmbientLight : Light
{
	public AmbientLight () { }
	public AmbientLight (Color color) { Color = color; }
}

public sealed class DirectionalLight : Light
{
	Vector3D _direction = new Vector3D (0, 0, -1);
	public DirectionalLight () { }
	public DirectionalLight (Color color, Vector3D direction) { Color = color; _direction = direction; }
	/// <summary>Le sens ou va la lumiere.</summary>
	public Vector3D Direction { get => _direction; set => Set (ref _direction, value); }
}

public sealed class PointLight : Light
{
	Point3D _position;
	double _c = 1, _l, _q;
	public PointLight () { }
	public PointLight (Color color, Point3D position) { Color = color; _position = position; }
	public Point3D Position { get => _position; set => Set (ref _position, value); }
	public double ConstantAttenuation { get => _c; set => Set (ref _c, value); }
	public double LinearAttenuation { get => _l; set => Set (ref _l, value); }
	public double QuadraticAttenuation { get => _q; set => Set (ref _q, value); }
}

// ---- visuels -----------------------------------------------------------------------------------------------

public abstract class Visual3D : Media3DObject
{
	Transform3D _transform;
	public Transform3D Transform { get => _transform; set => Attach (ref _transform, value); }
}

public sealed class ModelVisual3D : Visual3D
{
	Model3D _content;
	public ModelVisual3D () { Children.Changed += NotifyChanged; }
	public ModelVisual3D (Model3D content) : this () { Content = content; }
	public Model3D Content { get => _content; set => Attach (ref _content, value); }
	public Collection3D<Visual3D> Children { get; } = new ();
}
