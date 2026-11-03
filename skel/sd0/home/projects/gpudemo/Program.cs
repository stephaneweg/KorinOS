// gpudemo : des cartes animees, pour voir ce que le GPU change a une interface Avalonia. Le projet s'ouvre dans
// le Studio et se compile dans l'OS. Le meme programme tourne sur le processeur avec AVALONIA_ARMPI_GPU=0.
//     dotnet gpudemo.dll [--bench secondes] [--cards nombre] [--noshadows] [--cpu]
using System;
using Avalonia;

namespace GpuDemo;

public static class Program
{
	public static int Bench;
	public static int Cards = 24;
	public static bool NoShadows;

	[STAThread]
	public static int Main (string[] args)
	{
		for (int i = 0; i < args.Length; i++)
		{
			if (args[i] == "--bench") Bench = int.Parse (args[++i]);
			else if (args[i] == "--cards") Cards = int.Parse (args[++i]);
			else if (args[i] == "--noshadows") NoShadows = true;
			else if (args[i] == "--cpu") Environment.SetEnvironmentVariable ("AVALONIA_ARMPI_GPU", "0");
		}
		return BuildAvaloniaApp ().StartWithClassicDesktopLifetime (args);
	}

	public static AppBuilder BuildAvaloniaApp ()
		=> AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ();
}
