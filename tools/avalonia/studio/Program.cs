// avstudio : l'IDE .NET en Avalonia (docs/AVALONIA.md). Le meme .dll sous Windows et sous ARMPI ; un projet est
// un .csproj, celui que Visual Studio ouvre aussi.
//     avstudio [dossier du projet] [--run]
// --run : compile et lance le projet ouvert, comme le bouton Executer (essais sous QEMU).
using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;

namespace AvStudio;

public static class Program
{
	public static string? Folder;
	public static bool RunAtStart;

	[STAThread]
	public static int Main (string[] args)
	{
		if (args.Length > 0 && args[0] == "--selftest")
			return XamlText.SelfTest ();
		foreach (string a in args)
			if (a == "--run")
				RunAtStart = true;
			else if (Folder == null)
				Folder = a;
		AppDomain.CurrentDomain.UnhandledException += (_, e) => StudioLog.Write ("exception fatale : " + e.ExceptionObject);
		TaskScheduler.UnobservedTaskException += (_, e) =>
		{
			StudioLog.Write ("exception d'une tache : " + e.Exception);
			e.SetObserved ();
		};
		StudioLog.Write ("demarrage, " + Environment.OSVersion + ", " + Environment.ProcessorCount + " coeurs");
		try
		{
			return AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ().StartWithClassicDesktopLifetime (args);
		}
		catch (Exception e)
		{
			StudioLog.Write ("exception fatale : " + e);
			throw;
		}
	}
}

/// <summary>Le journal du Studio : $HOME/.avstudio/studio.log (sous Korin PC, SD0:/.avstudio/studio.log). Il garde
/// la pile de toute exception, fatale ou rattrapee, et chaque compilation et lancement.</summary>
public static class StudioLog
{
	static readonly object s_lock = new ();

	public static string Path
	{
		get
		{
			string home = Environment.GetEnvironmentVariable ("HOME") ?? Environment.GetFolderPath (Environment.SpecialFolder.UserProfile);
			return System.IO.Path.Combine (string.IsNullOrEmpty (home) ? "." : home, ".avstudio", "studio.log");
		}
	}

	public static void Write (string line)
	{
		try
		{
			lock (s_lock)
			{
				string p = Path;
				Directory.CreateDirectory (System.IO.Path.GetDirectoryName (p)!);
				if (File.Exists (p) && new FileInfo (p).Length > 512 * 1024)	// (le journal ne grossit pas sans fin)
					File.Move (p, p + ".1", true);
				File.AppendAllText (p, DateTime.Now.ToString ("yyyy-MM-dd HH:mm:ss.fff") + " " + line + Environment.NewLine);
			}
		}
		catch (Exception)
		{
			// (pas de journal : le Studio continue)
		}
		Console.Error.WriteLine ("avstudio : " + line);
	}
}
