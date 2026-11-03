// Renderer.cs -- une image de System.3D : la scene devient des sommets et des lots au format de gk_render
// (GpuKit, docs/V3D.md), donnes au GPU ou, sans lui, a Rasterizer.cs. Les shaders de GpuKit sont fixes
// (texel * couleur + couleur 2) : la lumiere se calcule ici, par sommet, comme dans teapot d'Onyx ; le diffus
// va dans la couleur, le speculaire et l'emis dans la couleur 2.
//
// GpuKit refuse un sommet hors de la bande de garde (|x|, |y| <= 4 w, w > 0, z >= -w) : un modele entier dans
// cette bande garde sa matrice (le GPU le transforme) ; les autres sont transformes et decoupes ici.
#nullable disable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.GpuKit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace System.Windows.Media.Media3D;

/// <summary>GKV_* : 32 octets.</summary>
internal struct GkVertex
{
	internal float X, Y, Z, W, S, T;
	internal uint Color, Color2;		// octets r g b a
}

/// <summary>GKB_* : 80 octets.</summary>
internal unsafe struct GkBatch
{
	internal uint First, Count;
	internal int Texture;
	internal uint Flags;
	internal fixed float Matrix[16];	// ligne par ligne : decoupe = M * v
}

/// <summary>GKF_* : 32 octets.</summary>
[StructLayout (LayoutKind.Sequential)]
internal struct GkFrame
{
	internal long Pixels;
	internal int W, H, Stride;
	internal uint Clear, Flags, Pad;
}

/// <summary>Ce qu'a coute la derniere image.</summary>
public struct FrameStats
{
	public int Triangles, Batches, Clipped;
	public bool Gpu;			// sinon : le processeur
	public double BuildMs, RenderMs;	// la scene en sommets ; le dessin, tampon d'etape compris
	public long GpuJobUs;			// le GPU seul, vu par le service (0 sans GPU)
}

/// <summary>Dessine une scene dans des pixels 0x00RRGGBB. Viewport3D s'en sert ; utilisable sans fenetre.</summary>
public sealed unsafe class Renderer3D : IDisposable
{
	const float Guard = 3.9f;		// (4 pour GpuKit : un peu moins, pour les arrondis)
	const int MaxLights = 8;

	struct LightData
	{
		internal int Kind;		// 1 directionnelle, 2 ponctuelle
		internal float R, G, B, X, Y, Z, C, L, Q;
	}
	struct Item
	{
		internal GeometryModel3D Model;
		internal Matrix3D World;
		internal MaterialData Front, Back;
		internal bool HasBack, Blend;
		internal double Depth;
	}

	readonly List<Item> _items = new ();
	readonly LightData[] _lights = new LightData[MaxLights];
	int _nLights;
	float _ambR, _ambG, _ambB;
	float _eyeX, _eyeY, _eyeZ;

	GkVertex* _v;
	int _nv, _capV;
	GkBatch* _b;
	int _nb, _capB;
	ImageTexture[] _batchTex = new ImageTexture[64];
	uint[] _col = new uint[1024], _col2 = new uint[1024];
	float* _z;
	uint* _back;
	int _zPixels;
	bool _gpuLost;
	long _ticks;

	public FrameStats Stats { get; private set; }

	/// <summary>Toujours le processeur, meme avec un GPU.</summary>
	public bool SoftwareOnly { get; set; }

	/// <summary>Le dernier code de gk_render (GK_E_*), 0 s'il a dessine.</summary>
	public long GpuError { get; private set; }

	public void Dispose ()
	{
		NativeMemory.Free (_v);
		NativeMemory.Free (_b);
		NativeMemory.Free (_z);
		NativeMemory.Free (_back);
		_v = null;
		_b = null;
		_z = null;
		_back = null;
		_capV = _capB = _zPixels = 0;
	}

	public void Render (Camera camera, IEnumerable<Visual3D> visuals, uint* pixels, int width, int height, int stride, uint clear)
	{
		long t0 = Stopwatch.GetTimestamp ();
		bool gpu = !SoftwareOnly && !_gpuLost;
		_nv = _nb = 0;
		_clipped = 0;
		if (camera != null)
			Build (camera, visuals, width, height, gpu);
		long t1 = Stopwatch.GetTimestamp ();
		var s = new FrameStats { Batches = _nb, Clipped = _clipped, Triangles = _nv / 3 };
		if (gpu && _nv <= GK.GK_MAX_VERTS && _nb <= GK.GK_MAX_BATCHES && width <= GK.GK_MAX_W && height <= GK.GK_MAX_H)
			s.Gpu = RenderGpu (pixels, width, height, stride, clear);
		if (s.Gpu)
		{
			long* info = stackalloc long[(int) GK.GKI_SIZE / 8];
			GK.gk_info ((long) info);
			s.GpuJobUs = info[GK.GKI_LAST_US / 8];
		}
		else
		{
			// a cote, puis d'un coup : Elegant compose la surface quand il veut, jamais une image a moitie dessinee
			// (cote GPU, le tampon d'etape de GpuKit fait de meme)
			if (_zPixels < width * height)
			{
				NativeMemory.Free (_z);
				NativeMemory.Free (_back);
				_z = (float*) NativeMemory.Alloc ((nuint) (width * height), sizeof (float));
				_back = (uint*) NativeMemory.Alloc ((nuint) (width * height), sizeof (uint));
				_zPixels = width * height;
			}
			Rasterizer.Draw (_v, _b, _nb, _batchTex, _back, width, height, width, clear, _z);
			for (int y = 0; y < height; y++)
				new ReadOnlySpan<uint> (_back + (long) y * width, width).CopyTo (new Span<uint> (pixels + (long) y * stride, width));
		}
		long t2 = Stopwatch.GetTimestamp ();
		s.BuildMs = (t1 - t0) * 1000.0 / Stopwatch.Frequency;
		s.RenderMs = (t2 - t1) * 1000.0 / Stopwatch.Frequency;
		Stats = s;
	}

	/// <summary>-> vrai si les pixels sont faits. Faux : au processeur, pour cette image ou pour de bon.</summary>
	bool RenderGpu (uint* pixels, int width, int height, int stride, uint clear)
	{
		var f = new GkFrame { Pixels = (long) pixels, W = width, H = height, Stride = stride, Clear = clear };
		long r = GK.gk_render ((long) &f, (long) _v, _nv, (long) _b, _nb);
		GpuError = r;
		if (r == 0)
			return true;
		if (r != GK.GK_E_INVAL)		// (pas de GPU, ou perdu : le dire une fois suffit)
			_gpuLost = true;
		return false;
	}

	// ---- la scene ----------------------------------------------------------------------------------------

	void Build (Camera camera, IEnumerable<Visual3D> visuals, int width, int height, bool gpu)
	{
		_items.Clear ();
		_nLights = 0;
		_ambR = _ambG = _ambB = 0;
		foreach (var v in visuals)
			Walk (v, Matrix3D.Identity);
		_eyeX = (float) camera.Position.X;
		_eyeY = (float) camera.Position.Y;
		_eyeZ = (float) camera.Position.Z;
		var vp = camera.View () * camera.Projection ((double) width / height);
		var eye = camera.Position;
		var look = camera.LookDirection;
		look.Normalize ();
		// les opaques d'abord ; les transparents de loin en pres, sans ecrire le Z
		for (int i = 0; i < _items.Count; i++)
		{
			var it = _items[i];
			if (it.Blend)
			{
				var c = it.World.Transform (new Point3D (0, 0, 0));
				it.Depth = Vector3D.DotProduct (c - eye, look);
				_items[i] = it;
			}
		}
		_items.Sort ((a, b) => a.Blend != b.Blend ? (a.Blend ? 1 : -1) : b.Depth.CompareTo (a.Depth));
		foreach (var it in _items)
		{
			bool mirror = it.World.Determinant3 < 0;
			Emit (it, in it.Front, it.World * vp, false, mirror, gpu);
			if (it.HasBack)
				Emit (it, in it.Back, it.World * vp, true, mirror, gpu);
		}
	}

	void Walk (Visual3D v, Matrix3D parent)
	{
		var m = v.Transform != null ? v.Transform.Value * parent : parent;
		if (v is ModelVisual3D mv)
		{
			if (mv.Content != null)
				Walk (mv.Content, m);
			foreach (var c in mv.Children)
				Walk (c, m);
		}
	}

	void Walk (Model3D model, Matrix3D parent)
	{
		var m = model.Transform != null ? model.Transform.Value * parent : parent;
		switch (model)
		{
		case Model3DGroup g:
			foreach (var c in g.Children)
				Walk (c, m);
			break;
		case GeometryModel3D g:
			if (g.Geometry is MeshGeometry3D && g.Material != null)
			{
				var it = new Item { Model = g, World = m };
				g.Material.Add (ref it.Front);
				if (g.BackMaterial != null)
				{
					g.BackMaterial.Add (ref it.Back);
					it.HasBack = true;
				}
				it.Blend = it.Front.A < 1 || (it.HasBack && it.Back.A < 1);
				_items.Add (it);
			}
			break;
		case AmbientLight a:
			_ambR += a.Color.R / 255f;
			_ambG += a.Color.G / 255f;
			_ambB += a.Color.B / 255f;
			break;
		case DirectionalLight d:
			if (_nLights < MaxLights)
			{
				var dir = -m.Transform (d.Direction);	// (vers la lumiere)
				dir.Normalize ();
				_lights[_nLights++] = new LightData { Kind = 1, R = d.Color.R / 255f, G = d.Color.G / 255f, B = d.Color.B / 255f,
					X = (float) dir.X, Y = (float) dir.Y, Z = (float) dir.Z };
			}
			break;
		case PointLight p:
			if (_nLights < MaxLights)
			{
				var at = m.Transform (p.Position);
				_lights[_nLights++] = new LightData { Kind = 2, R = p.Color.R / 255f, G = p.Color.G / 255f, B = p.Color.B / 255f,
					X = (float) at.X, Y = (float) at.Y, Z = (float) at.Z,
					C = (float) p.ConstantAttenuation, L = (float) p.LinearAttenuation, Q = (float) p.QuadraticAttenuation };
			}
			break;
		}
	}

	void ReserveVerts (int more)
	{
		if (_nv + more <= _capV)
			return;
		_capV = Math.Max (_capV * 2, _nv + more);
		_v = (GkVertex*) NativeMemory.Realloc (_v, (nuint) (_capV * sizeof (GkVertex)));
	}

	GkBatch* NewBatch ()
	{
		if (_nb == _capB)
		{
			_capB = Math.Max (_capB * 2, 64);
			_b = (GkBatch*) NativeMemory.Realloc (_b, (nuint) (_capB * sizeof (GkBatch)));
			Array.Resize (ref _batchTex, _capB);
		}
		return &_b[_nb++];
	}

	/// <summary>Une face du modele : ses couleurs, puis ses sommets et un lot.</summary>
	void Emit (in Item it, in MaterialData mat, Matrix3D wvp, bool back, bool mirror, bool gpu)
	{
		var d = ((MeshGeometry3D) it.Model.Geometry).Data;
		if (d.Index.Length == 0)
			return;
		int place = Classify (d, wvp);
		if (place < 0)
			return;
		if (_col.Length < d.Vertices)
		{
			_col = new uint[d.Vertices];
			_col2 = new uint[d.Vertices];
		}
		Light (d, it.World, in mat, back);
		uint flags = (back != mirror) ? (uint) GK.GK_B_CULL_FRONT : (uint) GK.GK_B_CULL_BACK;
		if (it.Blend)
			flags |= (uint) GK.GK_B_NOZWRITE | (uint) GK.GK_BLEND_ALPHA << (int) GK.GK_B_BLEND_SHIFT;
		int tex = -1;
		if (mat.Texture != null)
		{
			tex = gpu ? GpuTexture (mat.Texture) : 0;
			if (mat.Texture.Linear)
				flags |= (uint) GK.GK_B_LINEAR;
		}
		var b = NewBatch ();
		b->First = (uint) _nv;
		b->Texture = tex;
		_batchTex[_nb - 1] = mat.Texture;
		if (place == 0)
		{
			b->Flags = flags;
			StoreMatrix (b, wvp);
			Triangles (d);
		}
		else
		{
			b->Flags = flags | (uint) GK.GK_B_NOMATRIX;
			Clipped (d, wvp);
			_clipped++;
		}
		b->Count = (uint) _nv - b->First;
	}
	int _clipped;

	/// <summary>Une texture sans GPU garde -1 ; gk_render dira alors que le GPU manque.</summary>
	int GpuTexture (ImageTexture t)
	{
		if (t.GpuId >= 0)
			return t.GpuId;
		fixed (uint* p = t.Pixels)
			t.GpuId = (int) GK.gk_texture (-1, (long) p, t.Width, t.Height, t.Width);
		if (t.GpuId == GK.GK_E_NOGPU || t.GpuId == GK.GK_E_LOST)
			_gpuLost = true;
		return t.GpuId;
	}

	static void StoreMatrix (GkBatch* b, in Matrix3D m)
	{
		// v * m de WPF -> m^T * v de GpuKit, par colonnes de m
		float* o = b->Matrix;
		o[0] = (float) m.M11; o[1] = (float) m.M21; o[2] = (float) m.M31; o[3] = (float) m.OffsetX;
		o[4] = (float) m.M12; o[5] = (float) m.M22; o[6] = (float) m.M32; o[7] = (float) m.OffsetY;
		o[8] = (float) m.M13; o[9] = (float) m.M23; o[10] = (float) m.M33; o[11] = (float) m.OffsetZ;
		o[12] = (float) m.M14; o[13] = (float) m.M24; o[14] = (float) m.M34; o[15] = (float) m.M44;
	}

	/// <summary>La boite du modele apres la matrice -> 0 dans la bande, 1 a decouper, -1 hors de vue.</summary>
	static int Classify (MeshData d, in Matrix3D m)
	{
		bool inside = true;
		int outL = 0, outR = 0, outB = 0, outT = 0, outN = 0, outF = 0;
		for (int i = 0; i < 8; i++)
		{
			double x = (i & 1) != 0 ? d.MaxX : d.MinX, y = (i & 2) != 0 ? d.MaxY : d.MinY, z = (i & 4) != 0 ? d.MaxZ : d.MinZ;
			double cx = x * m.M11 + y * m.M21 + z * m.M31 + m.OffsetX, cy = x * m.M12 + y * m.M22 + z * m.M32 + m.OffsetY;
			double cz = x * m.M13 + y * m.M23 + z * m.M33 + m.OffsetZ, w = x * m.M14 + y * m.M24 + z * m.M34 + m.M44;
			if (cx < -w) outL++;
			if (cx > w) outR++;
			if (cy < -w) outB++;
			if (cy > w) outT++;
			if (cz < -w) outN++;
			if (cz > w) outF++;
			if (w <= 1e-5 || Math.Abs (cx) > Guard * w || Math.Abs (cy) > Guard * w || cz < -w || cz > w)
				inside = false;
		}
		if (outL == 8 || outR == 8 || outB == 8 || outT == 8 || outN == 8 || outF == 8)
			return -1;
		return inside ? 0 : 1;
	}

	/// <summary>La lumiere de chaque sommet, dans _col (ambiant + diffus) et _col2 (speculaire + emis).</summary>
	[MethodImpl (MethodImplOptions.AggressiveOptimization)]
	void Light (MeshData d, in Matrix3D world, in MaterialData mat, bool back)
	{
		// les normales suivent la transposee de l'inverse : les cofacteurs, au signe du determinant pres
		float m11 = (float) world.M11, m12 = (float) world.M12, m13 = (float) world.M13;
		float m21 = (float) world.M21, m22 = (float) world.M22, m23 = (float) world.M23;
		float m31 = (float) world.M31, m32 = (float) world.M32, m33 = (float) world.M33;
		float ox = (float) world.OffsetX, oy = (float) world.OffsetY, oz = (float) world.OffsetZ;
		float flip = (world.Determinant3 < 0) != back ? -1 : 1;
		float c11 = (m22 * m33 - m23 * m32) * flip, c12 = (m23 * m31 - m21 * m33) * flip, c13 = (m21 * m32 - m22 * m31) * flip;
		float c21 = (m13 * m32 - m12 * m33) * flip, c22 = (m11 * m33 - m13 * m31) * flip, c23 = (m12 * m31 - m11 * m32) * flip;
		float c31 = (m12 * m23 - m13 * m22) * flip, c32 = (m13 * m21 - m11 * m23) * flip, c33 = (m11 * m22 - m12 * m21) * flip;

		float dr = mat.Diffuse ? mat.R : 0, dg = mat.Diffuse ? mat.G : 0, db = mat.Diffuse ? mat.B : 0;
		float sr = mat.SR, sg = mat.SG, sb = mat.SB, power = mat.Power;
		uint alpha = (uint) (mat.A * 255 + 0.5f);
		float ambR = _ambR, ambG = _ambG, ambB = _ambB, ex = _eyeX, ey = _eyeY, ez = _eyeZ;
		bool spec = sr + sg + sb > 0 && power > 0;
		int nl = _nLights;
		bool position = spec;
		for (int l = 0; l < nl; l++)
			position |= _lights[l].Kind == 2;

		fixed (float* P = d.P, N = d.N)
		fixed (uint* col = _col, col2 = _col2)
		fixed (LightData* lights = _lights)
		{
			for (int i = 0; i < d.Vertices; i++)
			{
				float x = P[i * 3], y = P[i * 3 + 1], z = P[i * 3 + 2];
				float nx = N[i * 3], ny = N[i * 3 + 1], nz = N[i * 3 + 2];
				float wnx = nx * c11 + ny * c21 + nz * c31, wny = nx * c12 + ny * c22 + nz * c32, wnz = nx * c13 + ny * c23 + nz * c33;
				float il = 1 / MathF.Sqrt (wnx * wnx + wny * wny + wnz * wnz + 1e-20f);
				wnx *= il;
				wny *= il;
				wnz *= il;
				float wx = 0, wy = 0, wz = 0, vx = 0, vy = 0, vz = 0;
				if (position)
				{
					wx = x * m11 + y * m21 + z * m31 + ox;
					wy = x * m12 + y * m22 + z * m32 + oy;
					wz = x * m13 + y * m23 + z * m33 + oz;
					if (spec)
					{
						vx = ex - wx;
						vy = ey - wy;
						vz = ez - wz;
						float iv = 1 / MathF.Sqrt (vx * vx + vy * vy + vz * vz + 1e-20f);
						vx *= iv;
						vy *= iv;
						vz *= iv;
					}
				}
				float r = ambR, g = ambG, b = ambB, r2 = 0, g2 = 0, b2 = 0;
				for (int l = 0; l < nl; l++)
				{
					LightData* lt = &lights[l];
					float lx = lt->X, ly = lt->Y, lz = lt->Z, att = 1;
					if (lt->Kind == 2)
					{
						lx -= wx;
						ly -= wy;
						lz -= wz;
						float dist = MathF.Sqrt (lx * lx + ly * ly + lz * lz + 1e-20f), id = 1 / dist;
						lx *= id;
						ly *= id;
						lz *= id;
						att = 1 / (lt->C + lt->L * dist + lt->Q * dist * dist);
					}
					float dot = wnx * lx + wny * ly + wnz * lz;
					if (dot <= 0)
						continue;
					float k = dot * att;
					r += lt->R * k;
					g += lt->G * k;
					b += lt->B * k;
					if (spec)
					{
						float hx = lx + vx, hy = ly + vy, hz = lz + vz;
						float ih = 1 / MathF.Sqrt (hx * hx + hy * hy + hz * hz + 1e-20f);
						float nh = (wnx * hx + wny * hy + wnz * hz) * ih;
						if (nh > 0)
						{
							float s = MathF.Pow (nh, power) * att;
							r2 += lt->R * s;
							g2 += lt->G * s;
							b2 += lt->B * s;
						}
					}
				}
				col[i] = Byte (r * dr) | Byte (g * dg) << 8 | Byte (b * db) << 16 | alpha << 24;
				col2[i] = Byte (r2 * sr + mat.ER) | Byte (g2 * sg + mat.EG) << 8 | Byte (b2 * sb + mat.EB) << 16;
			}
		}
	}

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static uint Byte (float v) => v >= 1 ? 255u : v <= 0 ? 0u : (uint) (v * 255 + 0.5f);

	/// <summary>Le modele tel quel : le GPU le transforme par la matrice du lot.</summary>
	[MethodImpl (MethodImplOptions.AggressiveOptimization)]
	void Triangles (MeshData d)
	{
		ReserveVerts (d.Index.Length);
		GkVertex* o = _v + _nv;
		fixed (float* P = d.P, UV = d.UV)
		fixed (int* index = d.Index)
		fixed (uint* col = _col, col2 = _col2)
		{
			for (int i = 0; i < d.Index.Length; i++)
			{
				int k = index[i];
				float* p = P + k * 3;
				o->X = p[0];
				o->Y = p[1];
				o->Z = p[2];
				o->W = 1;
				o->S = UV[k * 2];
				o->T = UV[k * 2 + 1];
				o->Color = col[k];
				o->Color2 = col2[k];
				o++;
			}
		}
		_nv += d.Index.Length;
	}

	/// <summary>Un sommet pendant la decoupe : position de decoupe, tout le reste en flottants.</summary>
	struct Clip
	{
		internal float X, Y, Z, W, S, T, R, G, B, A, R2, G2, B2;
	}

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static float Plane (in Clip c, int plane) => plane switch
	{
		0 => c.Z + 0.9999f * c.W,	// pres
		1 => c.W - 1e-5f,		// (w > 0)
		2 => Guard * c.W - c.X,
		3 => Guard * c.W + c.X,
		4 => Guard * c.W - c.Y,
		5 => Guard * c.W + c.Y,
		_ => c.W - c.Z,			// loin
	};

	static void Mix (in Clip a, in Clip b, float k, out Clip o)
	{
		o.X = a.X + (b.X - a.X) * k;
		o.Y = a.Y + (b.Y - a.Y) * k;
		o.Z = a.Z + (b.Z - a.Z) * k;
		o.W = a.W + (b.W - a.W) * k;
		o.S = a.S + (b.S - a.S) * k;
		o.T = a.T + (b.T - a.T) * k;
		o.R = a.R + (b.R - a.R) * k;
		o.G = a.G + (b.G - a.G) * k;
		o.B = a.B + (b.B - a.B) * k;
		o.A = a.A + (b.A - a.A) * k;
		o.R2 = a.R2 + (b.R2 - a.R2) * k;
		o.G2 = a.G2 + (b.G2 - a.G2) * k;
		o.B2 = a.B2 + (b.B2 - a.B2) * k;
	}

	[MethodImpl (MethodImplOptions.AggressiveInlining)]
	static uint Pack (float r, float g, float b, float a) => (uint) r | (uint) g << 8 | (uint) b << 16 | (uint) a << 24;

	/// <summary>Le modele transforme et decoupe ici (Sutherland-Hodgman, un plan apres l'autre).</summary>
	[MethodImpl (MethodImplOptions.AggressiveOptimization)]
	void Clipped (MeshData d, in Matrix3D m)
	{
		const int Planes = 7;
		float m11 = (float) m.M11, m12 = (float) m.M12, m13 = (float) m.M13, m14 = (float) m.M14;
		float m21 = (float) m.M21, m22 = (float) m.M22, m23 = (float) m.M23, m24 = (float) m.M24;
		float m31 = (float) m.M31, m32 = (float) m.M32, m33 = (float) m.M33, m34 = (float) m.M34;
		float ox = (float) m.OffsetX, oy = (float) m.OffsetY, oz = (float) m.OffsetZ, ow = (float) m.M44;
		Clip* poly = stackalloc Clip[Planes + 3], next = stackalloc Clip[Planes + 3];
		fixed (float* P = d.P, UV = d.UV)
		fixed (int* index = d.Index)
		fixed (uint* col = _col, col2 = _col2)
		{
			for (int i = 0; i + 2 < d.Index.Length; i += 3)
			{
				int n = 3;
				for (int c = 0; c < 3; c++)
				{
					int k = index[i + c];
					float x = P[k * 3], y = P[k * 3 + 1], z = P[k * 3 + 2];
					uint a = col[k], b = col2[k];
					poly[c].X = x * m11 + y * m21 + z * m31 + ox;
					poly[c].Y = x * m12 + y * m22 + z * m32 + oy;
					poly[c].Z = x * m13 + y * m23 + z * m33 + oz;
					poly[c].W = x * m14 + y * m24 + z * m34 + ow;
					poly[c].S = UV[k * 2];
					poly[c].T = UV[k * 2 + 1];
					poly[c].R = a & 255;
					poly[c].G = (a >> 8) & 255;
					poly[c].B = (a >> 16) & 255;
					poly[c].A = a >> 24;
					poly[c].R2 = b & 255;
					poly[c].G2 = (b >> 8) & 255;
					poly[c].B2 = (b >> 16) & 255;
				}
				for (int pl = 0; pl < Planes && n >= 3; pl++)
				{
					int o = 0;
					float dPrev = Plane (in poly[n - 1], pl);
					for (int c = 0; c < n; c++)
					{
						Clip* cur = poly + c, prev = poly + (c + n - 1) % n;
						float dCur = Plane (in *cur, pl);
						if ((dPrev >= 0) != (dCur >= 0))
							Mix (in *prev, in *cur, dPrev / (dPrev - dCur), out next[o++]);
						if (dCur >= 0)
							next[o++] = *cur;
						dPrev = dCur;
					}
					Clip* t = poly;
					poly = next;
					next = t;
					n = o;
				}
				if (n < 3)
					continue;
				ReserveVerts ((n - 2) * 3);
				GkVertex* v = _v + _nv;
				for (int f = 1; f + 1 < n; f++)
				{
					Store (v++, in poly[0]);
					Store (v++, in poly[f]);
					Store (v++, in poly[f + 1]);
				}
				_nv += (n - 2) * 3;
			}
		}
	}

	static void Store (GkVertex* v, in Clip c)
	{
		v->X = c.X;
		v->Y = c.Y;
		v->Z = c.Z;
		v->W = c.W;
		v->S = c.S;
		v->T = c.T;
		v->Color = Pack (c.R, c.G, c.B, c.A);
		v->Color2 = Pack (c.R2, c.G2, c.B2, 0);
	}
}
