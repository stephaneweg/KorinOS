// AppBuilderDesktopExtensions.cs -- le seul type public d'Avalonia.Desktop, sous la meme identite
// qu'a Windows : une application compilee une fois appelle UsePlatformDetect et arrive ici sous ARMPI
// (docs/AVALONIA.md). Cette identite donne aussi l'acces aux internes d'Avalonia.Base (InternalsVisibleTo).
#nullable enable
using Avalonia.Armpi;

namespace Avalonia
{
	public static class AppBuilderDesktopExtensions
	{
		public static AppBuilder UsePlatformDetect (this AppBuilder builder)
		{
			builder.UseHarfBuzz ();
			ArmpiPlatform.Use (builder);
			builder.UseSkia ();
			builder.With (new Media.FontManagerOptions { DefaultFamilyName = "DejaVu Sans" });
			builder.AfterSetup (ArmpiTheme.Apply);
			return builder;
		}
	}
}
