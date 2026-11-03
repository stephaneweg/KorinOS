using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace WfHello;

public partial class MainForm : Form
{
	readonly bool _auto;
	int _ticks;

	public MainForm (bool auto)
	{
		_auto = auto;
		InitializeComponent ();
		osLabel.Text = RuntimeInformation.OSDescription + " / " + RuntimeInformation.RuntimeIdentifier;
		var a = typeof (Form).Assembly.GetName ();
		asmLabel.Text = a.Name + " " + a.Version + " " + Convert.ToHexString (a.GetPublicKeyToken ());
		Log ("demarre : " + osLabel.Text + " ; " + asmLabel.Text);
		if (_auto)
			timer.Start ();
	}

	static void Log (string s)
	{
		Console.WriteLine ("wfhello : " + s);
		File.AppendAllText (Path.Combine (AppContext.BaseDirectory, "wfhello.log"), s + "\n");
	}

	void OnAdd (object sender, EventArgs e)
	{
		list.Items.Add (name.Text);
		canvas.Invalidate ();
		Log ("ajoute : " + name.Text + ", " + list.Items.Count + " element(s)");
	}

	void OnChecked (object sender, EventArgs e) => canvas.Invalidate ();

	void OnPaintCanvas (object sender, PaintEventArgs e)
	{
		var g = e.Graphics;
		using var back = new SolidBrush (check.Checked ? Color.SteelBlue : Color.DarkSeaGreen);
		using var ink = new SolidBrush (Color.White);
		using var line = new Pen (Color.OrangeRed, 3);
		var r = canvas.ClientRectangle;
		g.FillRectangle (back, r);
		g.DrawLine (line, 0, 0, r.Width, r.Height);
		g.FillEllipse (ink, r.Width / 2 - 24, r.Height / 2 - 24, 48, 48);
		g.DrawRectangle (line, 8, 8, 60, 30);
		g.DrawString (list.Items.Count + " element(s)", canvas.Font, ink, 12, r.Height - 28);
	}

	void OnTick (object sender, EventArgs e)
	{
		_ticks++;
		if (_ticks == 1)
		{
			name.Text = "memes octets";
			check.Checked = true;
			add.PerformClick ();
			add.PerformClick ();
		}
		if (_ticks == 2)
			Log ("etat : " + list.Items.Count + " element(s), " + (check.Checked ? "coche" : "pas coche") + ", texte " + name.Text);
		if (_ticks == 40)
		{
			Log ("fin");
			Close ();
		}
	}
}
