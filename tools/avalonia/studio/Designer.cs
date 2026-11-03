// Designer.cs -- l'apercu du .axaml et la selection dedans. L'apercu vient du vrai Avalonia : un .axaml
// transforme (PreviewBuilder) puis lu par AvaloniaRuntimeXamlLoader. Chaque controle y porte son rang dans Tag.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using Armpi.Project;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace AvStudio;

/// <summary>Les controles d'Avalonia, trouves par reflexion : tous ceux qu'un .axaml peut nommer.</summary>
public static class ControlCatalog
{
	public const string Avalonia = "https://github.com/avaloniaui";
	public const string X = "http://schemas.microsoft.com/winfx/2006/xaml";

	static Dictionary<string, Type>? s_types;

	/// <summary>Les types publics derives de Control avec un constructeur sans argument.</summary>
	public static Dictionary<string, Type> Types
	{
		get
		{
			if (s_types != null)
				return s_types;
			s_types = new ();
			foreach (var t in typeof (Control).Assembly.GetExportedTypes ().Concat (typeof (Shape).Assembly.GetExportedTypes ()))
				if (typeof (Control).IsAssignableFrom (t) && !t.IsGenericTypeDefinition)
					s_types[t.Name] = t;
			return s_types;
		}
	}

	static readonly string[] s_first =
	{
		"Grid", "StackPanel", "DockPanel", "WrapPanel", "Canvas", "UniformGrid", "Border", "ScrollViewer", "TabControl", "Expander",
		"TextBlock", "Label", "TextBox", "Button", "CheckBox", "RadioButton", "ToggleButton", "ComboBox", "ListBox", "TreeView",
		"Slider", "ProgressBar", "NumericUpDown", "DatePicker", "Calendar", "Image", "Rectangle", "Ellipse", "Separator", "Menu",
	};

	/// <summary>La palette : les courants d'abord, puis tous les autres qu'on peut creer.</summary>
	public static IEnumerable<string> Palette ()
	{
		var rest = Types.Values.Where (t => !t.IsAbstract && t.GetConstructor (Type.EmptyTypes) != null && !typeof (TopLevel).IsAssignableFrom (t)
			&& t.Namespace is "Avalonia.Controls" or "Avalonia.Controls.Shapes").Select (t => t.Name);
		return s_first.Where (Types.ContainsKey).Concat (rest.Except (s_first).OrderBy (n => n, StringComparer.Ordinal));
	}

	/// <summary>Ce qu'un nouveau controle recoit, pour etre visible.</summary>
	public static string Snippet (string name) => name switch
	{
		"Button" or "CheckBox" or "RadioButton" or "ToggleButton" or "Label" => "<" + name + " Content=\"" + name + "\" />",
		"TextBlock" => "<TextBlock Text=\"Texte\" />",
		"Rectangle" or "Ellipse" => "<" + name + " Width=\"60\" Height=\"40\" Fill=\"{DynamicResource SystemAccentColor}\" />",
		"Border" => "<Border Padding=\"8\" BorderThickness=\"1\" BorderBrush=\"Gray\" />",
		_ => "<" + name + " />",
	};
}

/// <summary>Un user control du projet : son .axaml et sa classe.</summary>
public sealed record ProjectControl (string Name, string Namespace, string Path);

public static class PreviewBuilder
{
	static readonly XNamespace Av = ControlCatalog.Avalonia, Xn = ControlCatalog.X;
	static readonly string[] s_inert = { "Style", "ControlTheme", "DataTemplate", "ControlTemplate", "TreeDataTemplate", "ItemsPanelTemplate" };

	static bool IsInert (XElement e) => e.Ancestors ().Any (a => s_inert.Contains (a.Name.LocalName)
		|| a.Name.LocalName.EndsWith (".Resources") || a.Name.LocalName.EndsWith (".Styles") || a.Name.LocalName.EndsWith (".DataTemplates"));

	/// <summary>Le type d'Avalonia d'un element, null sinon.</summary>
	public static Type? TypeOf (XElement e) => e.Name.Namespace == Av && ControlCatalog.Types.TryGetValue (e.Name.LocalName, out var t) ? t : null;

	/// <summary>Les user controls du projet : un .axaml dont la classe x:Class derive de UserControl.</summary>
	public static List<ProjectControl> UserControls (CsProject p)
	{
		var list = new List<ProjectControl> ();
		foreach (var f in p.AvaloniaXaml)
		{
			string path = System.IO.Path.Combine (p.Directory, f);
			var doc = File.Exists (path) ? XamlText.Parse (File.ReadAllText (path)) : null;
			var cls = doc?.Root?.Attribute (Xn + "Class")?.Value;
			if (cls != null && doc!.Root!.Name.LocalName == "UserControl" && cls.Contains ('.'))
				list.Add (new ProjectControl (cls.Substring (cls.LastIndexOf ('.') + 1), cls.Substring (0, cls.LastIndexOf ('.')), path));
		}
		return list;
	}

	/// <summary>Le texte a donner au chargeur ; null si ce document n'a pas d'apercu (Application, Styles...).</summary>
	public static string? Build (string text, IReadOnlyList<ProjectControl> controls, int depth = 0)
	{
		var doc = XDocument.Parse (text);
		var root = doc.Root!;
		if (root.Name.LocalName is "Application" or "Styles" or "ResourceDictionary" or "Style")
			return null;
		root.Attributes (Xn + "Class").Remove ();
		var elements = doc.Descendants ().ToList ();
		for (int i = 0; i < elements.Count; i++)
			if (!elements[i].Name.LocalName.Contains ('.') && !IsInert (elements[i]))
				Mark (elements[i], i, controls, depth);
		foreach (var e in elements.Where (e => e.Document != null))
			e.Attributes (Xn + "DataType").Remove ();
		if (root.Name == Av + "Window")
			doc = new XDocument (Unwindow (root));
		return doc.ToString (SaveOptions.DisableFormatting);
	}

	static void Mark (XElement e, int id, IReadOnlyList<ProjectControl> controls, int depth)
	{
		var type = TypeOf (e);
		if (type != null)
		{
			if (depth == 0)
				e.SetAttributeValue ("Tag", "d" + id);
			foreach (var a in e.Attributes ().ToList ())
				if (!a.IsNamespaceDeclaration && a.Name.Namespace == XNamespace.None && type.GetEvent (a.Name.LocalName) != null)
					a.Remove ();
			return;
		}
		if (e.Name.Namespace == Av)
			return;
		string ns = Regex.Replace (e.Name.NamespaceName, "^(using:|clr-namespace:)", "").Split (';')[0];
		var control = controls.FirstOrDefault (c => c.Name == e.Name.LocalName && c.Namespace == ns);
		// (un controle du projet n'est pas charge ici : son .axaml en tient lieu)
		string? inner = null;
		if (control != null && depth < 4)
		{
			try { inner = Build (File.ReadAllText (control.Path), controls, depth + 1); }
			catch (Exception x) when (x is IOException or System.Xml.XmlException) { }
		}
		XElement box = new (Av + "Border", new XAttribute ("MinHeight", "24"));
		if (depth == 0)
			box.SetAttributeValue ("Tag", "d" + id);
		foreach (var name in new[] { "Width", "Height", "Margin", "HorizontalAlignment", "VerticalAlignment" })
			if (e.Attribute (name) is { } a)
				box.SetAttributeValue (name, a.Value);
		foreach (var a in e.Attributes ().Where (a => a.Name.Namespace != XNamespace.None && !a.IsNamespaceDeclaration && a.Name.LocalName.Contains ('.')))
			box.Add (new XAttribute (a));
		foreach (var a in e.Attributes ().Where (a => a.Name.Namespace == XNamespace.None && a.Name.LocalName.Contains ('.')))
			box.Add (new XAttribute (a));
		if (inner != null)
		{
			var body = XDocument.Parse (inner).Root!;
			if (body.Name.LocalName == "UserControl")
			{
				foreach (var a in body.Attributes ().Where (a => a.Name.LocalName is "Background"))
					box.SetAttributeValue ("Background", a.Value);
				box.Add (body.Elements ().Where (c => !c.Name.LocalName.Contains ('.')).Take (1));
			}
		}
		else
		{
			box.SetAttributeValue ("Background", "#20808080");
			box.Add (new XElement (Av + "TextBlock", new XAttribute ("Text", e.Name.LocalName), new XAttribute ("Margin", "4"), new XAttribute ("Opacity", "0.7")));
		}
		e.ReplaceWith (box);
	}

	/// <summary>Une fenetre ne se loge pas dans une autre : un Border, avec ce que le contenu en attend.</summary>
	static XElement Unwindow (XElement window)
	{
		var box = new XElement (Av + "Border");
		foreach (var a in window.Attributes ())
			if (a.IsNamespaceDeclaration || a.Name.LocalName is "Width" or "Height" or "Tag" or "FontSize" or "FontFamily" or "Padding" or "Background")
				box.Add (new XAttribute (a));
		if (box.Attribute ("Background") == null)
			box.SetAttributeValue ("Background", "{DynamicResource SystemRegionBrush}");
		foreach (var c in window.Nodes ().ToList ())
		{
			c.Remove ();
			if (c is XElement e && e.Name.LocalName.StartsWith ("Window."))
			{
				if (e.Name.LocalName is "Window.Styles" or "Window.Resources" or "Window.DataTemplates")
				{
					e.Name = Av + ("Border." + e.Name.LocalName.Substring (7));
					box.Add (e);
				}
			}
			else
				box.Add (c);
		}
		return box;
	}
}

/// <summary>L'apercu et, par-dessus, ce qui montre la selection et recoit la souris.</summary>
public sealed class DesignSurface : UserControl
{
	readonly ContentControl _host = new () { HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
	readonly Canvas _overlay = new () { Background = Brushes.Transparent };
	readonly Border _marker = new () { BorderThickness = new Thickness (1.5), IsVisible = false, IsHitTestVisible = false };
	readonly Border _card = new () { BoxShadow = BoxShadows.Parse ("0 2 12 0 #30000000"), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
	readonly Grid _stack = new () { HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
	readonly ScaleTransform _zoom = new ();
	readonly ScrollViewer _scroll = new () { HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto };
	int _selected = -1;

	public event Action<int>? Picked;

	public DesignSurface ()
	{
		_marker.Bind (Border.BorderBrushProperty, this.GetResourceObservable ("SystemAccentColor", o => o is Color c ? new SolidColorBrush (c) : Brushes.DodgerBlue));
		this.Bind (BackgroundProperty, this.GetResourceObservable ("SystemControlBackgroundChromeMediumLowBrush"));
		_overlay.Children.Add (_marker);
		_stack.Children.Add (_host);
		_stack.Children.Add (_overlay);
		Focusable = true;
		_card.Child = _stack;
		_scroll.Content = new LayoutTransformControl
		{
			Child = _card, LayoutTransform = _zoom, Margin = new Thickness (16),
			HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top,
		};
		Content = _scroll;
		SizeChanged += (_, _) => Fit ();
		_overlay.PointerPressed += (_, e) =>
		{
			Focus ();
			int id = HitAt (e.GetPosition (_overlay));
			if (id >= 0)
				Picked?.Invoke (id);
		};
		_host.LayoutUpdated += (_, _) =>
		{
			Fit ();
			PlaceMarker ();
		};
	}

	/// <summary>L'apercu entre dans la surface, sans jamais grossir.</summary>
	void Fit ()
	{
		double w = _host.Bounds.Width, room = Bounds.Width - 36;
		double scale = w > 0 && room > 0 ? Math.Min (1, room / w) : 1;
		if (Math.Abs (scale - _zoom.ScaleX) > 0.01)
			_zoom.ScaleX = _zoom.ScaleY = scale;
	}

	/// <summary>Le rang de l'element sous ce point du contenu (coordonnees de la fenetre), -1 s'il n'y en a pas.</summary>
	public int HitAtWindow (Point p, Visual from) => from.TranslatePoint (p, _overlay) is { } q ? HitAt (q) : -1;

	public bool Contains (Point p, Visual from) => from.TranslatePoint (p, this) is { } q && new Rect (Bounds.Size).Contains (q);

	/// <summary>Montre un .axaml ; une erreur de lecture sort par l'exception et l'apercu d'avant reste.</summary>
	public void Show (string xaml)
	{
		_host.Content = AvaloniaRuntimeXamlLoader.Parse<object> (xaml);
		Dispatcher.UIThread.Post (PlaceMarker, DispatcherPriority.Loaded);
	}

	public void Clear ()
	{
		_host.Content = null;
		Select (-1);
	}

	public Control? Selected => _selected < 0 ? null : Find (_host, "d" + _selected);

	public void Select (int id)
	{
		_selected = id;
		PlaceMarker ();
	}

	static Control? Find (Visual v, string tag)
	{
		if (v is Control { Tag: string t } c && t == tag)
			return c;
		foreach (var child in v.GetVisualChildren ())
			if (Find (child, tag) is { } found)
				return found;
		return null;
	}

	void PlaceMarker ()
	{
		var c = Selected;
		var at = c?.TranslatePoint (default, _overlay);
		_marker.IsVisible = at != null;
		if (at is not { } p)
			return;
		Canvas.SetLeft (_marker, p.X - 1);
		Canvas.SetTop (_marker, p.Y - 1);
		var size = c!.TranslatePoint (new Point (c.Bounds.Width, c.Bounds.Height), _overlay) is { } end ? end - p : new Point (c.Bounds.Width, c.Bounds.Height);
		_marker.Width = size.X + 2;
		_marker.Height = size.Y + 2;
	}

	int HitAt (Point p)
	{
		var (id, _) = Hit (_host, p);
		return id;
	}

	(int Id, bool Inside) Hit (Visual v, Point p)
	{
		var q = _overlay.TranslatePoint (p, v);
		if (q == null || !v.IsVisible)
			return (-1, false);
		bool inside = new Rect (v.Bounds.Size).Contains (q.Value);
		foreach (var child in v.GetVisualChildren ().Reverse ())
		{
			var (id, _) = Hit (child, p);
			if (id >= 0)
				return (id, true);
		}
		if (inside && v is Control { Tag: string t } && t.StartsWith ('d') && int.TryParse (t.AsSpan (1), out int found))
			return (found, true);
		return (-1, inside);
	}
}
