// Math3D.cs -- points, vecteurs et matrices de System.3D, aux noms de WPF : un vecteur ligne, v * M, la
// translation dans la derniere ligne. Le graphe de scene compte en double ; seul le rendu passe en float.
#nullable disable
using System;

namespace System.Windows.Media.Media3D;

public struct Vector3D
{
	public double X, Y, Z;
	public Vector3D (double x, double y, double z) { X = x; Y = y; Z = z; }
	public double Length => Math.Sqrt (X * X + Y * Y + Z * Z);
	public void Normalize ()
	{
		double l = Length;
		if (l > 0)
		{
			X /= l;
			Y /= l;
			Z /= l;
		}
	}
	public static double DotProduct (Vector3D a, Vector3D b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
	public static Vector3D CrossProduct (Vector3D a, Vector3D b) =>
		new Vector3D (a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
	public static Vector3D operator + (Vector3D a, Vector3D b) => new Vector3D (a.X + b.X, a.Y + b.Y, a.Z + b.Z);
	public static Vector3D operator - (Vector3D a, Vector3D b) => new Vector3D (a.X - b.X, a.Y - b.Y, a.Z - b.Z);
	public static Vector3D operator - (Vector3D a) => new Vector3D (-a.X, -a.Y, -a.Z);
	public static Vector3D operator * (Vector3D a, double k) => new Vector3D (a.X * k, a.Y * k, a.Z * k);
	public static Vector3D operator * (double k, Vector3D a) => a * k;
}

public struct Point3D
{
	public double X, Y, Z;
	public Point3D (double x, double y, double z) { X = x; Y = y; Z = z; }
	public static Point3D operator + (Point3D p, Vector3D v) => new Point3D (p.X + v.X, p.Y + v.Y, p.Z + v.Z);
	public static Point3D operator - (Point3D p, Vector3D v) => new Point3D (p.X - v.X, p.Y - v.Y, p.Z - v.Z);
	public static Vector3D operator - (Point3D a, Point3D b) => new Vector3D (a.X - b.X, a.Y - b.Y, a.Z - b.Z);
}

public struct Matrix3D
{
	public double M11, M12, M13, M14, M21, M22, M23, M24, M31, M32, M33, M34, OffsetX, OffsetY, OffsetZ, M44;

	public Matrix3D (double m11, double m12, double m13, double m14, double m21, double m22, double m23, double m24,
		double m31, double m32, double m33, double m34, double offsetX, double offsetY, double offsetZ, double m44)
	{
		M11 = m11; M12 = m12; M13 = m13; M14 = m14;
		M21 = m21; M22 = m22; M23 = m23; M24 = m24;
		M31 = m31; M32 = m32; M33 = m33; M34 = m34;
		OffsetX = offsetX; OffsetY = offsetY; OffsetZ = offsetZ; M44 = m44;
	}

	public static Matrix3D Identity => new Matrix3D (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

	public static Matrix3D Translation (double x, double y, double z) =>
		new Matrix3D (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1);

	public static Matrix3D Scaling (double x, double y, double z) =>
		new Matrix3D (x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1);

	/// <summary>Autour d'un axe passant par l'origine ; angle en degres, sens direct.</summary>
	public static Matrix3D Rotation (Vector3D axis, double degrees)
	{
		axis.Normalize ();
		double a = degrees * Math.PI / 180, c = Math.Cos (a), s = Math.Sin (a), t = 1 - c;
		double x = axis.X, y = axis.Y, z = axis.Z;
		return new Matrix3D (
			t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0,
			t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0,
			t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0,
			0, 0, 0, 1);
	}

	/// <summary>a puis b : v * a * b.</summary>
	public static Matrix3D Multiply (Matrix3D a, Matrix3D b) => a * b;

	public static Matrix3D operator * (Matrix3D a, Matrix3D b) => new Matrix3D (
		a.M11 * b.M11 + a.M12 * b.M21 + a.M13 * b.M31 + a.M14 * b.OffsetX,
		a.M11 * b.M12 + a.M12 * b.M22 + a.M13 * b.M32 + a.M14 * b.OffsetY,
		a.M11 * b.M13 + a.M12 * b.M23 + a.M13 * b.M33 + a.M14 * b.OffsetZ,
		a.M11 * b.M14 + a.M12 * b.M24 + a.M13 * b.M34 + a.M14 * b.M44,
		a.M21 * b.M11 + a.M22 * b.M21 + a.M23 * b.M31 + a.M24 * b.OffsetX,
		a.M21 * b.M12 + a.M22 * b.M22 + a.M23 * b.M32 + a.M24 * b.OffsetY,
		a.M21 * b.M13 + a.M22 * b.M23 + a.M23 * b.M33 + a.M24 * b.OffsetZ,
		a.M21 * b.M14 + a.M22 * b.M24 + a.M23 * b.M34 + a.M24 * b.M44,
		a.M31 * b.M11 + a.M32 * b.M21 + a.M33 * b.M31 + a.M34 * b.OffsetX,
		a.M31 * b.M12 + a.M32 * b.M22 + a.M33 * b.M32 + a.M34 * b.OffsetY,
		a.M31 * b.M13 + a.M32 * b.M23 + a.M33 * b.M33 + a.M34 * b.OffsetZ,
		a.M31 * b.M14 + a.M32 * b.M24 + a.M33 * b.M34 + a.M34 * b.M44,
		a.OffsetX * b.M11 + a.OffsetY * b.M21 + a.OffsetZ * b.M31 + a.M44 * b.OffsetX,
		a.OffsetX * b.M12 + a.OffsetY * b.M22 + a.OffsetZ * b.M32 + a.M44 * b.OffsetY,
		a.OffsetX * b.M13 + a.OffsetY * b.M23 + a.OffsetZ * b.M33 + a.M44 * b.OffsetZ,
		a.OffsetX * b.M14 + a.OffsetY * b.M24 + a.OffsetZ * b.M34 + a.M44 * b.M44);

	public Point3D Transform (Point3D p) => new Point3D (
		p.X * M11 + p.Y * M21 + p.Z * M31 + OffsetX,
		p.X * M12 + p.Y * M22 + p.Z * M32 + OffsetY,
		p.X * M13 + p.Y * M23 + p.Z * M33 + OffsetZ);

	/// <summary>Une direction : sans la translation.</summary>
	public Vector3D Transform (Vector3D v) => new Vector3D (
		v.X * M11 + v.Y * M21 + v.Z * M31,
		v.X * M12 + v.Y * M22 + v.Z * M32,
		v.X * M13 + v.Y * M23 + v.Z * M33);

	/// <summary>Le determinant de la partie 3 x 3 : negatif, la matrice retourne les faces.</summary>
	internal double Determinant3 =>
		M11 * (M22 * M33 - M23 * M32) - M12 * (M21 * M33 - M23 * M31) + M13 * (M21 * M32 - M22 * M31);
}
