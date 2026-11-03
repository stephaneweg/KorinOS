// uidemo : une fenetre decrite par MainForm.ui, compilee par pibuild : aucun XML n'est lu a l'execution.
//     pibuild /dotnet/test/uidemo uidemo
using System;
using System.Windows.Forms;

partial class MainForm
{
	public MainForm () { InitializeComponent (); }

	void OnSaluer (object sender, EventArgs e) => journal.Items.Add ("Bonjour, " + nom.Text + " !");
	void OnSaisie (object sender, EventArgs e) => clear.Enabled = !clear.Enabled;
	void OnEffacer (object sender, EventArgs e) => journal.Items.Clear ();

	static void Main (string[] args)
	{
		var f = new MainForm ();
		Console.WriteLine ("uidemo : JIT " + (long) System.Runtime.JitInfo.GetCompilationTime ().TotalMilliseconds + " ms avant Run");
		f.Shown += (s, e) => { if (args.Length > 0) Application.Exit (); };
		Application.Run (f);
		Console.WriteLine ("uidemo : fin");
	}
}
