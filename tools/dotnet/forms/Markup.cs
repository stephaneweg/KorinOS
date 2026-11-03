// Markup.cs -- le systeme « a la WPF » d'ARMPI : un fichier .ui en XML decrit une fenetre ou un
// controle, lu au lancement, sur les controles de ce System.Windows.Forms donc sur UIKit :
//
//   <Form Text="Bonjour" Width="420" Height="260">
//     <StackPanel Dock="Fill" Padding="12" Spacing="8">
//       <Label Text="Votre nom :"/>
//       <TextBox Name="nom"/>
//       <Button Text="Saluer" Click="OnSaluer"/>
//       <Include Source="pied.ui"/>
//     </StackPanel>
//   </Form>
//
// Un element est un controle : ceux d'ici, ou une classe du programme (son nom simple). Un attribut
// est une propriete (entier, booleen, enumeration, « x,y », « l,h ») ou un evenement, dont la
// valeur nomme une methode du code du programme. Name relie aussi le controle au champ de ce nom.
// Sous la racine d'une fenetre, <MenuStrip> donne ses menus :
//
//   <MenuStrip>
//     <ToolStripMenuItem Text="Fichier">
//       <ToolStripMenuItem Text="Ouvrir" ShortcutKeys="Control|O" Click="OnOuvrir"/>
//       <ToolStripSeparator/>
//     </ToolStripMenuItem>
//   </MenuStrip>
#nullable disable
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;

namespace System.Windows.Forms.Markup;

public static class UiLoader
{
	/// <summary>Lit un .ui -> sa racine ; code : l'objet qui porte les methodes et les champs.</summary>
	public static Control Load (string path, object code = null)
	{
		var root = XDocument.Load (path).Root;
		return Build (root, code, Path.GetDirectoryName (Path.GetFullPath (path)));
	}

	public static T Load<T> (string path, object code = null) where T : Control => (T) Load (path, code);

	/// <summary>Remplit un controle deja cree (UserControl, Form du programme) depuis un .ui.</summary>
	public static void Fill (Control target, string path, object code = null)
	{
		var root = XDocument.Load (path).Root;
		string dir = Path.GetDirectoryName (Path.GetFullPath (path));
		Apply (target, root, code ?? target, dir);
	}

	/// <summary>L'apercu d'un editeur : le texte d'un .ui dans host, sans code : les evenements sont
	/// ignores. -> la taille de sa racine.</summary>
	public static Size Preview (Control host, string text, string dir)
	{
		var root = XDocument.Parse (text).Root;
		host.Size = new Size ((int?) root.Attribute ("Width") ?? host.Width, (int?) root.Attribute ("Height") ?? host.Height);
		s_preview = true;
		try
		{
			foreach (var child in root.Elements ())
				if (child.Name.LocalName != "MenuStrip")
					host.Controls.Add (Build (child, null, dir));
		}
		finally { s_preview = false; }
		return host.Size;
	}

	[ThreadStatic] static bool s_preview;

	static Control Build (XElement e, object code, string dir)
	{
		string name = e.Name.LocalName;
		if (name == "Include")
			return Load (Path.Combine (dir, (string) e.Attribute ("Source")), code);
		Type t = FindType (name, code);
		if (t == null || !typeof (Control).IsAssignableFrom (t))
			throw new InvalidOperationException (".ui : controle inconnu : " + name);
		var c = (Control) Activator.CreateInstance (t);
		Apply (c, e, code ?? c, dir);
		return c;
	}

	static void Apply (Control c, XElement e, object code, string dir)
	{
		foreach (var a in e.Attributes ())
			Set (c, a.Name.LocalName, a.Value, code);
		foreach (var child in e.Elements ())
		{
			string tag = child.Name.LocalName;
			if (tag == "MenuStrip" && c is Form form)
				form.MainMenuStrip = Menu (child, code);
			else if (tag == "Item" && c is ListControl l)
				l.Items.Add (child.Value);
			else if (tag == "Panel1" && c is SplitContainer s1)
				Apply (s1.Panel1, child, code, dir);
			else if (tag == "Panel2" && c is SplitContainer s2)
				Apply (s2.Panel2, child, code, dir);
			else if (tag == "Node" && c is TreeView t)
				t.Nodes.Add (Node (child));
			else if (tag == "Column" && c is DataGridView g)
				g.Columns.Add (new DataGridViewColumn ((string) child.Attribute ("HeaderText"), (int?) child.Attribute ("Width") ?? 100));
			else if (tag == "Row" && c is DataGridView r)
				r.Rows.Add (child.Value.Split ('|'));
			else if (child.Name.LocalName == "Column" && c is ListView v)
				v.Columns.Add (new ColumnHeader { Text = (string) child.Attribute ("Text"), Width = (int?) child.Attribute ("Width") ?? 100 });
			else
				c.Controls.Add (Build (child, code, dir));
		}
		if (!ReferenceEquals (code, c))
			Link (c, c.Name, code);
	}

	static void Link (object c, string name, object code)
	{
		if (code == null || name.Length == 0)
			return;
		var f = code.GetType ().GetField (name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
		if (f != null && f.FieldType.IsInstanceOfType (c))
			f.SetValue (code, c);
	}

	static MenuStrip Menu (XElement e, object code)
	{
		var strip = new MenuStrip ();
		foreach (var child in e.Elements ("ToolStripMenuItem"))
			strip.Items.Add (Item (child, code));
		return strip;
	}

	static ToolStripMenuItem Item (XElement e, object code)
	{
		var m = new ToolStripMenuItem ();
		foreach (var a in e.Attributes ())
			Set (m, a.Name.LocalName, a.Value, code);
		foreach (var child in e.Elements ())
		{
			if (child.Name.LocalName == "ToolStripSeparator")
				m.DropDownItems.Add ("-");
			else if (child.Name.LocalName == "ToolStripMenuItem")
				m.DropDownItems.Add (Item (child, code));
			else
				throw new InvalidOperationException (".ui : un menu ne prend pas " + child.Name.LocalName);
		}
		Link (m, m.Name, code);
		return m;
	}

	static TreeNode Node (XElement e)
	{
		var n = new TreeNode ((string) e.Attribute ("Text"));
		foreach (var child in e.Elements ("Node"))
			n.Nodes.Add (Node (child));
		return n;
	}

	static void Set (object c, string name, string value, object code)
	{
		Type t = c.GetType ();
		var ev = t.GetEvent (name);
		if (ev != null && s_preview)
			return;
		if (ev != null)
		{
			var m = code?.GetType ().GetMethod (value, BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
			if (m == null)
				throw new InvalidOperationException (".ui : methode inconnue : " + value);
			ev.AddEventHandler (c, m.IsStatic ? Delegate.CreateDelegate (ev.EventHandlerType, m) : Delegate.CreateDelegate (ev.EventHandlerType, code, m));
			return;
		}
		var p = t.GetProperty (name);
		if (p == null || !p.CanWrite)
			throw new InvalidOperationException (".ui : propriete inconnue : " + t.Name + "." + name);
		p.SetValue (c, Convert (value, p.PropertyType));
	}

	static object Convert (string s, Type t)
	{
		if (t == typeof (string))
			return s;
		if (t.IsEnum)
			return Enum.Parse (t, s.Replace ('|', ','), true);
		if (t == typeof (Padding))
			return new Padding (int.Parse (s, CultureInfo.InvariantCulture));
		if (t == typeof (Point))
		{
			var v = Ints (s);
			return new Point (v[0], v[1]);
		}
		if (t == typeof (Size))
		{
			var v = Ints (s);
			return new Size (v[0], v[1]);
		}
		return System.Convert.ChangeType (s, t, CultureInfo.InvariantCulture);
	}

	static int[] Ints (string s) => s.Split (',').Select (x => int.Parse (x.Trim (), CultureInfo.InvariantCulture)).ToArray ();

	static Type FindType (string name, object code)
	{
		Type t = typeof (Control).Assembly.GetType ("System.Windows.Forms." + name)
			?? Type.GetType ("System.Windows.Media.Media3D." + name + ", System.3D");
		if (t != null)
			return t;
		Assembly a = code?.GetType ().Assembly ?? Assembly.GetEntryAssembly ();
		return a?.GetTypes ().FirstOrDefault (x => x.Name == name);
	}
}
