// xamlload : le test du concepteur (docs/AVALONIA.md) : un fichier .axaml lu et compile a l'execution par
// AvaloniaRuntimeXamlLoader, sans XAML compile dans le programme. Un concepteur qui veut tout Avalonia en a besoin.
//     dotnet xamlload.dll fichier.axaml [--log journal] [--quit secondes]
using System;
using System.Diagnostics;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

namespace XamlLoad;

public sealed class App : Application
{
	public override void Initialize () => Styles.Add (new FluentTheme ());

	public override void OnFrameworkInitializationCompleted ()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
		{
			var clock = Stopwatch.StartNew ();
			var root = AvaloniaRuntimeXamlLoader.Parse<Control> (File.ReadAllText (Program.File!));
			Program.Say ("xaml charge en " + clock.ElapsedMilliseconds + " ms : " + root.GetType ().FullName);
			desktop.MainWindow = root as Window ?? new Window { Title = "xamlload", Width = 600, Height = 400, Content = root };
			desktop.MainWindow.Opened += (_, _) => Program.Say ("fenetre ouverte");
			if (Program.Quit > 0)
				DispatcherTimer.RunOnce (() => desktop.MainWindow.Close (), TimeSpan.FromSeconds (Program.Quit));
		}
		base.OnFrameworkInitializationCompleted ();
	}
}

public static class Program
{
	public static string? File;
	public static string? Log;
	public static int Quit;

	public static void Say (string line)
	{
		Console.WriteLine (line);
		if (Log != null)
			System.IO.File.AppendAllText (Log, line + "\n");
	}

	[STAThread]
	public static int Main (string[] args)
	{
		for (int i = 0; i < args.Length; i++)
		{
			if (args[i] == "--log") Log = args[++i];
			else if (args[i] == "--quit") Quit = int.Parse (args[++i]);
			else File = args[i];
		}
		return AppBuilder.Configure<App> ().UsePlatformDetect ().LogToTrace ().StartWithClassicDesktopLifetime (args);
	}
}
