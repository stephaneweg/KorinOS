using Avalonia.Controls;

namespace AvDemo;

public partial class AboutWindow : Window
{
	public AboutWindow ()
	{
		InitializeComponent ();
		OkButton.Click += (_, _) => Close ();
	}
}
