// avdemo : une vraie application Avalonia, compilee une fois par dotnet build, sans rien d'ARMPI.
//     dotnet avdemo.dll [--auto] [--bench secondes] [--log fichier]
using System;
using System.IO;
using Avalonia;

namespace AvDemo;

public sealed class DemoOptions
{
	public bool Auto;
	public int Bench;
	public string? Log;
	public readonly DateTime Start = DateTime.UtcNow;

	public void Say (string line)
	{
		string text = (int) (DateTime.UtcNow - Start).TotalMilliseconds + " ms : " + line;
		Console.WriteLine (text);
		if (Log != null)
			File.AppendAllText (Log, text + "\n");
	}
}

public static class Program
{
	public static DemoOptions Options = new ();

	[STAThread]
	public static int Main (string[] args)
	{
		for (int i = 0; i < args.Length; i++)
		{
			if (args[i] == "--auto") Options.Auto = true;
			else if (args[i] == "--bench") Options.Bench = int.Parse (args[++i]);
			else if (args[i] == "--log") Options.Log = args[++i];
		}
		Options.Say ("main");
		return BuildAvaloniaApp ().StartWithClassicDesktopLifetime (args);
	}

	public static AppBuilder BuildAvaloniaApp ()
		=> AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ();
}
