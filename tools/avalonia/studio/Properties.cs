// Properties.cs -- les proprietes et les evenements d'un element, par reflexion sur Avalonia : tout controle est couvert,
// ceux des bibliotheques aussi. La valeur ecrite va dans le .axaml, celle du controle affiche sert d'indication.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace AvStudio;

public sealed class PropertiesPanel : UserControl
{
	static readonly string[] s_common =
	{
		"Width", "Height", "MinWidth", "MinHeight", "Margin", "Padding", "HorizontalAlignment", "VerticalAlignment",
		"Content", "Text", "Header", "Background", "Foreground", "BorderBrush", "BorderThickness", "CornerRadius",
		"FontSize", "FontWeight", "Orientation", "Spacing", "RowDefinitions", "ColumnDefinitions", "RowSpacing", "ColumnSpacing",
		"IsVisible", "IsEnabled", "Opacity", "Classes",
	};

	static readonly (string Title, string[] Names)[] s_groups =
	{
		("DISPOSITION", new[] { "Width", "Height", "MinWidth", "MinHeight", "Margin", "Padding", "HorizontalAlignment", "VerticalAlignment", "Orientation", "Spacing", "RowDefinitions", "ColumnDefinitions", "RowSpacing", "ColumnSpacing" }),
		("CONTENU", new[] { "Content", "Text", "Header", "Classes" }),
		("APPARENCE", new[] { "Background", "Foreground", "BorderBrush", "BorderThickness", "CornerRadius", "FontSize", "FontWeight", "Opacity" }),
		("COMPORTEMENT", new[] { "IsVisible", "IsEnabled" }),
	};

	readonly TextBox _search = new () { Classes = { "prop" } };
	readonly StackPanel _rows = new () { Spacing = 3 };
	readonly TextBlock _title = new () { Text = "Rien de sélectionné", FontWeight = FontWeight.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis };
	readonly bool _events;
	XElement? _element;
	Control? _live;

	/// <summary>Une valeur changee : attribut, valeur (null : retiree).</summary>
	public event Action<string, string?>? Changed;
	/// <summary>Un evenement a relier : son nom, la signature de sa methode.</summary>
	public event Action<string, string>? HandlerRequested;

	public PropertiesPanel (bool events)
	{
		_events = events;
		_search.PlaceholderText = events ? "Chercher un événement" : "Chercher une propriété";
		_search.TextChanged += (_, _) => Fill ();
		var top = new StackPanel { Margin = new Thickness (10, 8, 10, 4), Spacing = 6 };
		top.Children.Add (_title);
		top.Children.Add (_search);
		var dock = new DockPanel ();
		DockPanel.SetDock (top, Dock.Top);
		dock.Children.Add (top);
		dock.Children.Add (new ScrollViewer { Content = _rows, Padding = new Thickness (10, 0, 10, 10) });
		Content = dock;
	}

	public void Show (XElement? element, Control? live)
	{
		_element = element;
		_live = live;
		Fill ();
	}

	void Fill ()
	{
		_rows.Children.Clear ();
		_title.Text = _element == null ? "Rien de sélectionné" : _element.Name.LocalName + (_element.Attribute ("Name")?.Value is { } n ? " : " + n : "");
		if (_element == null)
			return;
		if (_events)
			FillEvents ();
		else
			FillProperties ();
	}

	void FillProperties ()
	{
		var e = _element!;
		var type = PreviewBuilder.TypeOf (e);
		string filter = (_search.Text ?? "").Trim ();
		AddRow ("Name", typeof (string), null);
		var seen = new HashSet<string> { "Name", "Tag" };
		foreach (var a in e.Attributes ().Where (a => !a.IsNamespaceDeclaration && a.Name.Namespace == XNamespace.None))
			if (seen.Add (a.Name.LocalName) && Match (a.Name.LocalName, filter))
				AddRow (a.Name.LocalName, PropertyType (type, a.Name.LocalName), type);
		if (e.Parent != null)
		{
			string[] attached = e.Parent.Name.LocalName switch
			{
				"Grid" => new[] { "Grid.Row", "Grid.Column", "Grid.RowSpan", "Grid.ColumnSpan" },
				"DockPanel" => new[] { "DockPanel.Dock" },
				"Canvas" => new[] { "Canvas.Left", "Canvas.Top" },
				_ => Array.Empty<string> (),
			};
			foreach (var name in attached)
				if (seen.Add (name) && Match (name, filter))
					AddRow (name, name == "DockPanel.Dock" ? typeof (Dock) : typeof (string), null);
		}
		if (type == null)
			return;
		var all = AvaloniaPropertyRegistry.Instance.GetRegistered (type).Where (p => !p.IsReadOnly && !p.IsAttached).ToDictionary (p => p.Name, p => p, StringComparer.Ordinal);
		if (filter.Length > 0)
		{
			foreach (var name in all.Keys.OrderBy (n => n, StringComparer.Ordinal).Where (n => Match (n, filter)).Take (80))
				if (seen.Add (name))
					AddRow (name, all[name].PropertyType, type);
			return;
		}
		foreach (var (title, group) in s_groups)
		{
			var names = group.Where (n => all.ContainsKey (n) && !seen.Contains (n)).ToList ();
			if (names.Count == 0)
				continue;
			_rows.Children.Add (new TextBlock { Text = title, Classes = { "section" } });
			foreach (var name in names)
			{
				seen.Add (name);
				AddRow (name, all[name].PropertyType, type);
			}
		}
	}

	static bool Match (string name, string filter) => filter.Length == 0 || name.Contains (filter, StringComparison.OrdinalIgnoreCase);

	static Type? PropertyType (Type? owner, string name)
	{
		if (owner == null)
			return typeof (string);
		return AvaloniaPropertyRegistry.Instance.GetRegistered (owner).FirstOrDefault (p => p.Name == name)?.PropertyType ?? typeof (string);
	}

	string? Current (string name)
	{
		if (_live == null)
			return null;
		var p = AvaloniaPropertyRegistry.Instance.GetRegistered (_live.GetType ()).FirstOrDefault (x => x.Name == name);
		if (p == null)
			return null;
		var v = _live.GetValue (p);
		return v is null or Control ? null : v.ToString ();
	}

	void AddRow (string name, Type? type, Type? owner)
	{
		var e = _element!;
		string? value = e.Attribute (name)?.Value ?? (name.Contains ('.') ? null : null);
		Control editor;
		type = type == null ? typeof (string) : Nullable.GetUnderlyingType (type) ?? type;
		if (type == typeof (bool))
		{
			var box = new CheckBox { IsThreeState = true, IsChecked = value == null ? null : value.Equals ("True", StringComparison.OrdinalIgnoreCase) };
			box.IsCheckedChanged += (_, _) => Changed?.Invoke (name, box.IsChecked switch { true => "True", false => "False", _ => null });
			editor = box;
		}
		else if (type.IsEnum)
		{
			var box = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch, Classes = { "prop" } };
			box.Items.Add ("");
			foreach (var n in Enum.GetNames (type))
				box.Items.Add (n);
			box.SelectedItem = value ?? "";
			box.SelectionChanged += (_, _) =>
			{
				if (box.SelectedItem is string s && s != (value ?? ""))
					Changed?.Invoke (name, s.Length == 0 ? null : s);
			};
			editor = box;
		}
		else
		{
			var box = new TextBox { Text = value, PlaceholderText = Current (name), Classes = { "prop" } };
			void Commit ()
			{
				string? v = string.IsNullOrEmpty (box.Text) ? null : box.Text;
				if (v != value)
				{
					value = v;
					Changed?.Invoke (name, v);
				}
			}
			box.LostFocus += (_, _) => Commit ();
			box.KeyDown += (_, k) =>
			{
				if (k.Key == Key.Return)
					Commit ();
			};
			editor = box;
		}
		_rows.Children.Add (Row (name, editor));
	}

	static Grid Row (string name, Control editor)
	{
		var row = new Grid { ColumnDefinitions = new ColumnDefinitions ("100,*"), ColumnSpacing = 6 };
		var label = new TextBlock { Text = name, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis, FontSize = 12, Opacity = 0.8 };
		ToolTip.SetTip (label, name);
		Grid.SetColumn (editor, 1);
		row.Children.Add (label);
		row.Children.Add (editor);
		return row;
	}

	void FillEvents ()
	{
		var e = _element!;
		var type = PreviewBuilder.TypeOf (e);
		if (type == null)
			return;
		string filter = (_search.Text ?? "").Trim ();
		var events = type.GetEvents (BindingFlags.Public | BindingFlags.Instance).Where (v => v.EventHandlerType != null && Match (v.Name, filter));
		// (ceux que le .axaml utilise d'abord, puis ceux que le type declare lui-meme)
		foreach (var v in events.OrderBy (v => e.Attribute (v.Name) == null).ThenBy (v => v.DeclaringType == type ? 0 : 1).ThenBy (v => v.Name, StringComparer.Ordinal).Take (60))
		{
			string name = v.Name, signature = Signature (v);
			var box = new TextBox { Text = e.Attribute (name)?.Value, Classes = { "prop" } };
			var go = new Button { Content = e.Attribute (name) == null ? "+" : "→", Padding = new Thickness (8, 0), VerticalAlignment = VerticalAlignment.Stretch, Classes = { "tool" } };
			ToolTip.SetTip (go, e.Attribute (name) == null ? "Créer la méthode" : "Aller à la méthode");
			go.Click += (_, _) => HandlerRequested?.Invoke (name, signature);
			box.KeyDown += (_, k) =>
			{
				if (k.Key == Key.Return)
					Changed?.Invoke (name, string.IsNullOrEmpty (box.Text) ? null : box.Text);
			};
			var line = new Grid { ColumnDefinitions = new ColumnDefinitions ("*,Auto"), ColumnSpacing = 4 };
			Grid.SetColumn (go, 1);
			line.Children.Add (box);
			line.Children.Add (go);
			_rows.Children.Add (Row (name, line));
		}
	}

	static string TypeName (Type t) => (t.FullName ?? t.Name).Replace ('+', '.');

	/// <summary>Les parametres de la methode, d'apres le delegue de l'evenement.</summary>
	static string Signature (EventInfo v)
	{
		var p = v.EventHandlerType!.GetMethod ("Invoke")!.GetParameters ();
		return string.Join (", ", p.Select ((x, i) => (i == 0 && x.ParameterType == typeof (object) ? "object?" : TypeName (x.ParameterType)) + " " + (x.Name ?? "a" + i)));
	}
}
