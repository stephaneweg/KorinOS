using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Avalonia.Threading;

namespace AvDemo;

public partial class MainWindow : Window
{
	readonly ObservableCollection<string> _items = new () { "Lire le rapport", "Essayer sous QEMU" };
	readonly DemoOptions _options;
	readonly Stopwatch _clock = Stopwatch.StartNew ();
	int _frames;
	double _lastReport;

	public MainWindow () : this (new DemoOptions ()) { }

	public MainWindow (DemoOptions options)
	{
		_options = options;
		InitializeComponent ();
		Tasks.ItemsSource = _items;
		AddButton.Click += (_, _) => AddEntry ();
		RemoveButton.Click += (_, _) => RemoveSelected ();
		OpenButton.Click += async (_, _) => await OpenFile ();
		SaveButton.Click += async (_, _) => await SaveFile ();
		AboutButton.Click += async (_, _) => await ShowAbout ();
		WindowButton.Click += (_, _) => ShowSecond ();
		Entry.KeyDown += (_, e) =>
		{
			if (e.Key == Avalonia.Input.Key.Return)
				AddEntry ();
		};
		Effort.PropertyChanged += (_, e) =>
		{
			if (e.Property == Avalonia.Controls.Primitives.RangeBase.ValueProperty)
				EffortText.Text = "Effort : " + (int) Effort.Value;
		};
		Tasks.SelectionChanged += (_, _) => Status.Text = Tasks.SelectedItem as string ?? "prêt";
		Opened += OnOpened;
		Closed += (_, _) => _options.Say ("fin");
	}

	void AddEntry ()
	{
		string text = (Entry.Text ?? "").Trim ();
		if (text.Length == 0)
			return;
		if (Urgent.IsChecked == true)
			text = "! " + text;
		_items.Add (text);
		Entry.Text = "";
		Status.Text = _items.Count + " tâches";
		_options.Say ("ajoute : " + text + ", " + _items.Count + " element(s)");
	}

	void RemoveSelected ()
	{
		if (Tasks.SelectedItem is string s)
		{
			_items.Remove (s);
			_options.Say ("retire : " + s + ", " + _items.Count + " element(s)");
		}
	}

	async Task OpenFile ()
	{
		var files = await StorageProvider.OpenFilePickerAsync (new FilePickerOpenOptions { Title = "Ouvrir des tâches" });
		if (files.Count == 0)
		{
			_options.Say ("ouvrir : annule");
			return;
		}
		await using var stream = await files[0].OpenReadAsync ();
		using var reader = new StreamReader (stream);
		int lines = 0;
		while (await reader.ReadLineAsync () is { } line)
			if (line.Length > 0)
			{
				_items.Add (line);
				lines++;
			}
		_options.Say ("ouvert : " + files[0].Name + ", " + lines + " ligne(s)");
	}

	async Task SaveFile ()
	{
		var file = await StorageProvider.SaveFilePickerAsync (new FilePickerSaveOptions { Title = "Enregistrer les tâches", SuggestedFileName = "taches.txt" });
		if (file == null)
		{
			_options.Say ("enregistrer : annule");
			return;
		}
		await using var stream = await file.OpenWriteAsync ();
		await using var writer = new StreamWriter (stream);
		foreach (var item in _items)
			await writer.WriteLineAsync (item);
		_options.Say ("enregistre : " + file.Name + ", " + _items.Count + " element(s)");
	}

	async Task ShowAbout (bool autoClose = false)
	{
		var about = new AboutWindow ();
		if (autoClose)
			DispatcherTimer.RunOnce (about.Close, TimeSpan.FromSeconds (1));
		await about.ShowDialog (this);
		_options.Say ("dialogue ferme");
	}

	Window ShowSecond ()
	{
		var second = new Window
		{
			Title = "Seconde fenêtre", Width = 360, Height = 200,
			Content = new TextBlock { Text = "Une autre fenêtre du même programme.", Margin = new Avalonia.Thickness (20), TextWrapping = Avalonia.Media.TextWrapping.Wrap },
		};
		second.Closed += (_, _) => _options.Say ("seconde fenetre fermee");
		second.Show ();
		_options.Say ("seconde fenetre ouverte");
		return second;
	}

	void OnOpened (object? sender, EventArgs e)
	{
		_options.Say ("fenetre ouverte : " + RuntimeInformation.OSDescription + " / " + RuntimeInformation.RuntimeIdentifier
			+ " ; theme " + ActualThemeVariant + " ; Avalonia " + typeof (Window).Assembly.GetName ().Version);
		if (_options.Bench > 0)
			TopLevel.GetTopLevel (this)!.RequestAnimationFrame (OnFrame);
		else
			TopLevel.GetTopLevel (this)!.RequestAnimationFrame (OnSlide);
		if (_options.Auto)
			DispatcherTimer.RunOnce (AutoRun, TimeSpan.FromMilliseconds (500));
	}

	void Slide (double seconds)
	{
		double w = Stage.Bounds.Width - Box.Width;
		if (w <= 0)
			return;
		double t = seconds % 2;
		Canvas.SetLeft (Box, (t < 1 ? t : 2 - t) * w);
	}

	void OnSlide (TimeSpan t)
	{
		Slide (t.TotalSeconds);
		TopLevel.GetTopLevel (this)?.RequestAnimationFrame (OnSlide);
	}

	void OnFrame (TimeSpan t)
	{
		double now = _clock.Elapsed.TotalSeconds;
		Slide (now);
		_frames++;
		if (now - _lastReport >= 5)
		{
			_options.Say ("images : " + (_frames / (now - _lastReport)).ToString ("F1") + " par seconde");
			_frames = 0;
			_lastReport = now;
		}
		if (now >= _options.Bench)
			Close ();
		else
			TopLevel.GetTopLevel (this)?.RequestAnimationFrame (OnFrame);
	}

	async void AutoRun ()
	{
		Entry.Text = "memes octets";
		AddEntry ();
		Urgent.IsChecked = true;
		Entry.Text = "encore";
		AddEntry ();
		Tasks.SelectedIndex = 0;
		RemoveSelected ();
		_options.Say ("etat : " + _items.Count + " element(s), " + string.Join (" | ", _items));
		var second = ShowSecond ();
		await Task.Delay (1000);
		second.Close ();
		await ShowAbout (true);
		DispatcherTimer.RunOnce (Close, TimeSpan.FromSeconds (1));
	}
}
