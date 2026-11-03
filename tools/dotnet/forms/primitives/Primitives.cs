// Primitives.cs -- System.Windows.Forms.Primitives d'ARMPI : a Windows, ces types ne sont pas dans
// System.Windows.Forms, et un binaire compile les y cherche (docs/WINFORMS-PORTABLE.md).
#nullable disable
using System.Reflection;

[assembly: AssemblyVersion ("10.0.0.0")]

namespace System.Windows.Forms;

public enum HighDpiMode { DpiUnaware, SystemAware, PerMonitor, PerMonitorV2, DpiUnawareGdiScaled }

public struct Padding
{
	public int Left { get; set; }
	public int Top { get; set; }
	public int Right { get; set; }
	public int Bottom { get; set; }
	public Padding (int all) { Left = Top = Right = Bottom = all; }
	public Padding (int left, int top, int right, int bottom) { Left = left; Top = top; Right = right; Bottom = bottom; }
	public int Horizontal => Left + Right;
	public int Vertical => Top + Bottom;
}
