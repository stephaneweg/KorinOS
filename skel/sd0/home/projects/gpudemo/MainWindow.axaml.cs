using System;
using System.Collections.Generic;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace GpuDemo;

public partial class MainWindow : Window
{
	sealed class Card
	{
		public required Border View;
		public required RotateTransform Turn;
		public double X, Y, SpeedX, SpeedY, Spin;
	}

	const double CardWidth = 150, CardHeight = 92;
	static readonly BoxShadows Shadow = BoxShadows.Parse ("0 8 22 0 #A0000000");
	readonly List<Card> _cards = new ();
	readonly Random _random = new (7);
	readonly Stopwatch _clock = Stopwatch.StartNew (), _run = Stopwatch.StartNew ();
	TimeSpan _last;
	int _frames;

	public MainWindow ()
	{
		InitializeComponent ();
		Cards.Value = Program.Cards;
		Shadows.IsChecked = !Program.NoShadows;
		Cards.PropertyChanged += (_, e) =>
		{
			if (e.Property == Slider.ValueProperty)
				Fit ();
		};
		Shadows.IsCheckedChanged += (_, _) =>
		{
			foreach (var c in _cards)
				c.View.BoxShadow = Shadows.IsChecked == true ? Shadow : default;
		};
		Opened += (_, _) =>
		{
			Fit ();
			RequestAnimationFrame (Frame);
		};
	}

	// autant de cartes que le curseur en demande
	void Fit ()
	{
		int want = (int) Cards.Value;
		CardCount.Text = want.ToString ();
		while (_cards.Count > want)
		{
			Scene.Children.Remove (_cards[^1].View);
			_cards.RemoveAt (_cards.Count - 1);
		}
		while (_cards.Count < want)
			_cards.Add (NewCard (_cards.Count));
	}

	Card NewCard (int n)
	{
		double hue = n * 47 % 360;
		var turn = new RotateTransform ();
		var view = new Border
		{
			Width = CardWidth,
			Height = CardHeight,
			CornerRadius = new CornerRadius (14),
			BoxShadow = Shadows.IsChecked == true ? Shadow : default,
			RenderTransform = turn,
			Background = new LinearGradientBrush
			{
				StartPoint = new RelativePoint (0, 0, RelativeUnit.Relative),
				EndPoint = new RelativePoint (1, 1, RelativeUnit.Relative),
				GradientStops =
				{
					new GradientStop (HsvColor.ToRgb (hue, 0.75, 0.95), 0),
					new GradientStop (HsvColor.ToRgb ((hue + 60) % 360, 0.85, 0.55), 1),
				},
			},
			Child = new TextBlock
			{
				Text = "Carte " + (n + 1),
				Foreground = Brushes.White,
				FontSize = 18,
				HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
				VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
			},
		};
		Scene.Children.Add (view);
		return new Card
		{
			View = view,
			Turn = turn,
			X = _random.NextDouble () * 500,
			Y = _random.NextDouble () * 300,
			SpeedX = (_random.NextDouble () - 0.5) * 220,
			SpeedY = (_random.NextDouble () - 0.5) * 220,
			Spin = (_random.NextDouble () - 0.5) * 120,
		};
	}

	void Frame (TimeSpan now)
	{
		double dt = Math.Min ((now - _last).TotalSeconds, 0.05);
		_last = now;
		double width = Scene.Bounds.Width - CardWidth, height = Scene.Bounds.Height - CardHeight;
		foreach (var c in _cards)
		{
			c.X += c.SpeedX * dt;
			c.Y += c.SpeedY * dt;
			if (c.X < 0 || c.X > width) c.SpeedX = -c.SpeedX;
			if (c.Y < 0 || c.Y > height) c.SpeedY = -c.SpeedY;
			c.X = Math.Clamp (c.X, 0, Math.Max (width, 0));
			c.Y = Math.Clamp (c.Y, 0, Math.Max (height, 0));
			Canvas.SetLeft (c.View, c.X);
			Canvas.SetTop (c.View, c.Y);
			c.Turn.Angle += c.Spin * dt;
		}
		_frames++;
		if (_clock.ElapsedMilliseconds >= 1000)
		{
			double rate = _frames * 1000.0 / _clock.ElapsedMilliseconds;
			Stats.Text = rate.ToString ("F1") + " images/s";
			Console.WriteLine ("gpudemo : " + _cards.Count + " cartes, " + rate.ToString ("F1") + " images/s");
			_frames = 0;
			_clock.Restart ();
		}
		if (Program.Bench > 0 && _run.Elapsed.TotalSeconds >= Program.Bench)
		{
			Close ();
			return;
		}
		RequestAnimationFrame (Frame);
	}
}
