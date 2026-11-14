using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;

namespace AvStudio;

public partial class App : Application
{
	public override void Initialize () => AvaloniaXamlLoader.Load (this);

	public override void OnFrameworkInitializationCompleted ()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
		{
			var main = new MainWindow (Program.Folder);
			desktop.MainWindow = main;
			// une exception du thread de l'interface : au journal et dans la Sortie, le Studio reste ouvert
			Dispatcher.UIThread.UnhandledException += (_, e) =>
			{
				StudioLog.Write ("exception rattrapee : " + e.Exception);
				main.ShowFault (e.Exception);
				e.Handled = true;
			};
		}
		base.OnFrameworkInitializationCompleted ();
	}
}
