// formsdemo : essai de System.Windows.Forms et du .ui sous ARMPI (docs/DOTNET.md).
//     /dotnet/dotnet /dotnet/test/formsdemo.dll [--once]
// --once : ferme a la premiere minuterie, apres avoir dit ce que le JIT a compile (le demarrage, mesure).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime;
using System.Windows.Forms;
using System.Windows.Forms.Markup;

/// <summary>Un controle dessine a la main : la souris trace, C efface.</summary>
class Ardoise : Control
{
	readonly List<Point> points = new ();
	bool trace;

	protected override void OnPaint (PaintEventArgs e)
	{
		var g = e.Graphics;
		g.Clear (ThemeColors.Surface);
		g.DrawRectangle (new Pen (ThemeColors.Line), 0, 0, Width, Height);
		g.DrawString ("La souris trace, C efface", null, new SolidBrush (ThemeColors.TextDim), 8, 6);
		g.FillEllipse (new SolidBrush (ThemeColors.Accent), Width - 34, 8, 24, 16);
		var p = new Pen (ThemeColors.Text, 2);
		for (int i = 1; i < points.Count; i++)
			if (points[i] != Point.Empty && points[i - 1] != Point.Empty)
				g.DrawLine (p, points[i - 1], points[i]);
	}

	protected override void OnMouseDown (MouseEventArgs e)
	{
		trace = true;
		points.Add (Point.Empty);
		points.Add (e.Location);
		Invalidate ();
	}

	protected override void OnMouseMove (MouseEventArgs e)
	{
		if (!trace)
			return;
		points.Add (e.Location);
		Invalidate ();
	}

	protected override void OnMouseUp (MouseEventArgs e) => trace = false;

	protected override void OnKeyDown (KeyEventArgs e)
	{
		if (e.KeyCode != Keys.C)
			return;
		points.Clear ();
		Invalidate ();
		e.Handled = true;
	}
}

class Demo
{
	TextBox nom;
	Label reponse;
	ListBox journal;
	ProgressBar barre;
	TreeView arbre;
	TabControl onglets;
	DataGridView grille;
	DateTimePicker date;
	int ticks;
	static bool once;

	void OnSaluer (object sender, EventArgs e)
	{
		reponse.Text = "Bonjour, " + nom.Text + " !";
		journal.Items.Add ("salue : " + nom.Text);
	}

	void OnMessage (object sender, EventArgs e)
	{
		var r = MessageBox.Show ("Une boite de message de WinForms, dessinee par UIKit.", "Demo", MessageBoxButtons.OKCancel);
		journal.Items.Add ("message : " + r);
	}

	void OnOuvrir (object sender, EventArgs e)
	{
		using var d = new OpenFileDialog { InitialDirectory = "SD0:/", Filter = "Textes|*.txt|Tout|*.*" };
		if (d.ShowDialog () == DialogResult.OK)
			journal.Items.Add ("fichier : " + d.FileName);
	}

	void OnArdoise (object sender, EventArgs e)
	{
		var f = new Form { Text = "Ardoise", Width = 320, Height = 240 };
		f.Controls.Add (new Ardoise { Dock = DockStyle.Fill, Cursor = Cursors.Cross });
		f.FormClosed += (s, a) => journal.Items.Add ("ardoise fermee");
		f.Show ();
		journal.Items.Add ("ardoise ouverte");
	}

	void OnControles (object sender, EventArgs e)
	{
		string dir = Path.GetDirectoryName (typeof (Demo).Assembly.Location);
		UiLoader.Load<Form> (Path.Combine (dir, "widgets.ui"), this).Show ();
		arbre.ExpandAll ();
		journal.Items.Add ("controles ouverts");
	}

	void OnNoeud (object sender, TreeViewEventArgs e) => journal.Items.Add ("noeud : " + e.Node?.Text);
	void OnOnglet (object sender, EventArgs e) => journal.Items.Add ("onglet : " + onglets.SelectedTab?.Text);
	void OnLigne (object sender, EventArgs e) => journal.Items.Add ("ligne : " + grille.CurrentRow?.Cells[0]);
	void OnDate (object sender, EventArgs e) => journal.Items.Add ("date : " + date.Value.ToString ("yyyy-MM-dd"));
	void OnOutil (object sender, EventArgs e) => journal.Items.Add ("outil : " + ((ToolStripButton) sender).Text);

	void OnCouleur (object sender, EventArgs e)
	{
		using var d = new ColorDialog ();
		if (d.ShowDialog () == DialogResult.OK)
			journal.Items.Add ("couleur : " + d.Color.ToArgb ().ToString ("X8"));
	}

	void OnTick (object sender, EventArgs e)
	{
		if (once)
		{
			once = false;
			Console.WriteLine ("formsdemo : JIT " + (long) JitInfo.GetCompilationTime ().TotalMilliseconds + " ms, "
				+ JitInfo.GetCompiledMethodCount () + " methodes, " + JitInfo.GetCompiledILBytes () / 1024 + " Ko d'IL");
			Application.Exit ();
		}
		ticks = (ticks + 5) % 105;
		barre.Value = ticks;
	}

	static void Main (string[] args)
	{
		once = args.Length > 0 && args[0] == "--once";
		Application.EnableVisualStyles ();
		var d = new Demo ();
		string dir = Path.GetDirectoryName (typeof (Demo).Assembly.Location);
		var f = UiLoader.Load<Form> (Path.Combine (dir, "formsdemo.ui"), d);
		var fichier = new ToolStripMenuItem ("Fichier");
		fichier.DropDownItems.Add (new ToolStripMenuItem ("Saluer", null, d.OnSaluer));
		f.MainMenuStrip = new MenuStrip ();
		f.MainMenuStrip.Items.Add (fichier);
		var t = new Timer { Interval = 300 };
		t.Tick += d.OnTick;
		t.Start ();
		Console.WriteLine ("formsdemo : .ui lu, fenetre ouverte");
		Application.Run (f);
		Console.WriteLine ("formsdemo : fin");
	}
}
