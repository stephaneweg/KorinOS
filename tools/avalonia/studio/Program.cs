// avstudio : l'IDE .NET en Avalonia (docs/AVALONIA.md). Le meme .dll sous Windows et sous ARMPI ; un projet est
// un .csproj, celui que Visual Studio ouvre aussi.
//     avstudio [dossier du projet]
using System;
using Avalonia;

namespace AvStudio;

public static class Program
{
	public static string? Folder;

	[STAThread]
	public static int Main (string[] args)
	{
		if (args.Length > 0 && args[0] == "--selftest")
			return XamlText.SelfTest ();
		if (args.Length > 0)
			Folder = args[0];
		return AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ().StartWithClassicDesktopLifetime (args);
	}
}
