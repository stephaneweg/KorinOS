// Views.cs -- le code des vues decrites par les .ui : la barre laterale, l'authentification, l'editeur d'une
// requete et sa reponse, un environnement, une collection. Une seule vue de chaque sorte existe : l'onglet choisi
// s'y lie (Bind), et ce qu'on y saisit va dans son modele.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows.Forms;

partial class Sidebar : UserControl
{
	public MainForm App { set => list.App = value; get => list.App; }
	public int Mode => list.Mode;
	public void Rebuild () => list.Rebuild ();

	public void SetMode (int m)
	{
		list.Mode = m;
		if (mode.SelectedIndex != m)
			mode.SelectedIndex = m;
		add.Visible = m != 2;
		clear.Visible = m == 2;
		filter.Text = "";
		list.Filter = "";
		list.Rebuild ();
	}

	void OnMode (object s, EventArgs e) { if (mode.SelectedIndex != list.Mode) SetMode (mode.SelectedIndex); }
	void OnClear (object s, EventArgs e) => App.ClearHistory ();

	void OnAdd (object s, EventArgs e)
	{
		if (list.Mode == 0)
			App.NewCollection ();
		else
			App.NewEnv ();
	}

	void OnFilter (object s, EventArgs e)
	{
		list.Filter = filter.Text;
		list.Rebuild ();
	}
}

partial class AuthPanel : UserControl
{
	static readonly string[] Help =
	{
		"", "This request is sent without authorization.", "The token is sent as the Authorization header: Bearer <token>.",
		"The username and the password are sent as the Authorization header.", "The key and its value are sent as a header, or added to the URL's query.",
	};
	bool _forColl, _binding;
	Auth _auth;
	public event EventHandler Changed;
	/// <summary>Ce que la requete herite, dit quand le type est « Inherit ».</summary>
	public string InheritText { get; set; } = "";

	public AuthPanel ()
	{
		InitializeComponent ();
		Fill ();
	}

	/// <summary>Une collection n'herite de rien : pas de choix « Inherit ».</summary>
	public bool ForCollection
	{
		get => _forColl;
		set { _forColl = value; Fill (); }
	}

	void Fill ()
	{
		type.Items.Clear ();
		type.Items.AddRange (Auth.Names.Skip (_forColl ? 1 : 0).Cast<object> ().ToArray ());
	}

	public void Bind (Auth a)
	{
		_auth = a;
		_binding = true;
		type.SelectedIndex = Math.Max (0, (int) a.Type - (_forColl ? 1 : 0));
		where.SelectedIndex = a.InQuery ? 1 : 0;
		Sync ();
		_binding = false;
	}

	public void Sync ()
	{
		if (_auth == null)
			return;
		bool was = _binding;
		_binding = true;
		var t = _auth.Type;
		row1.Visible = t == AuthType.Bearer || t == AuthType.Basic || t == AuthType.ApiKey;
		row2.Visible = t == AuthType.Basic || t == AuthType.ApiKey;
		row3.Visible = t == AuthType.ApiKey;
		field2.Visible = t == AuthType.ApiKey;
		password.Visible = t == AuthType.Basic;
		tip.Visible = row1.Visible;
		label1.Text = t == AuthType.Bearer ? "Token" : t == AuthType.Basic ? "Username" : "Key";
		label2.Text = t == AuthType.Basic ? "Password" : "Value";
		field1.Text = t == AuthType.Bearer ? _auth.Token : t == AuthType.Basic ? _auth.User : _auth.Key;
		field2.Text = _auth.Value;
		password.Text = _auth.Pass;
		help.Text = t == AuthType.Inherit ? InheritText : Help[(int) t];
		_binding = was;
	}

	void OnType (object s, EventArgs e)
	{
		if (_binding || _auth == null)
			return;
		_auth.Type = (AuthType) (type.SelectedIndex + (_forColl ? 1 : 0));
		Sync ();
		Changed?.Invoke (this, EventArgs.Empty);
	}

	void OnWhere (object s, EventArgs e)
	{
		if (_binding || _auth == null)
			return;
		_auth.InQuery = where.SelectedIndex == 1;
		Changed?.Invoke (this, EventArgs.Empty);
	}

	void OnField (object s, EventArgs e)
	{
		if (_binding || _auth == null)
			return;
		switch (_auth.Type)
		{
		case AuthType.Bearer:
			_auth.Token = field1.Text;
			break;
		case AuthType.Basic:
			_auth.User = field1.Text;
			_auth.Pass = password.Text;
			break;
		case AuthType.ApiKey:
			_auth.Key = field1.Text;
			_auth.Value = field2.Text;
			break;
		}
		Changed?.Invoke (this, EventArgs.Empty);
	}
}

partial class RequestView : UserControl
{
	public MainForm App { get; set; }
	bool _binding;
	Tab T => App.Cur;
	public ResponseView Response => response;
	public int Split { get => split.SplitterDistance; set => split.SplitterDistance = value; }

	public void Bind (Tab t)
	{
		_binding = true;
		var r = t.Req;
		name.Text = r.Name;
		method.SelectedIndex = Math.Max (0, Array.FindIndex (Request.Methods, m => Util.EqI (m, r.Method)));
		url.Text = r.Url;
		queries.SetRows (r.Params);
		headers.SetRows (r.Headers);
		form.SetRows (r.Form);
		urlenc.SetRows (r.UrlEnc);
		tests.SetRows (r.Tests);
		captures.SetRows (r.Captures);
		auth.InheritText = App.InheritText (t);
		auth.Bind (r.Auth);
		bodyMode.SelectedIndex = (int) r.BodyMode;
		rawLang.SelectedIndex = (int) r.RawLang;
		raw.Text = r.Raw;
		follow.Checked = r.FollowRedirects;
		verify.Checked = !r.Insecure;
		timeout.Text = r.TimeoutMs != 0 ? r.TimeoutMs.ToString () : "";
		binaryName.Text = r.Binary.Length > 0 ? r.Binary : "No file chosen: its bytes are sent as the body.";
		BodyShown ();
		sections.SelectedIndex = t.ReqSection;
		if (t.Split > 0)
			split.SplitterDistance = t.Split;
		_binding = false;
		SendButton ();
	}

	public void SendButton () => send.Text = T?.Job != null ? "Cancel" : "Send";
	public void FocusUrl () => url.Focus ();
	public void SetName (string s) { _binding = true; name.Text = s; _binding = false; }
	public void SetAuthHelp (string s) { auth.InheritText = s; auth.Sync (); }

	void Edited ()
	{
		if (!_binding)
			App.Edited ();
	}

	void BodyShown ()
	{
		var t = T;
		if (t == null)
			return;
		var m = t.Req.BodyMode;
		noBody.Visible = m == BodyMode.None;
		form.Visible = m == BodyMode.Form;
		urlenc.Visible = m == BodyMode.UrlEncoded;
		raw.Visible = m == BodyMode.Raw;
		binaryRow.Visible = m == BodyMode.Binary;
		rawLang.Visible = m == BodyMode.Raw;
		beautify.Visible = m == BodyMode.Raw;
		bodyBulk.Visible = m == BodyMode.Form || m == BodyMode.UrlEncoded;
	}

	void OnName (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.Name = name.Text;
		Edited ();
	}

	void OnMethod (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.Method = Request.Methods[Math.Clamp (method.SelectedIndex, 0, Request.Methods.Length - 1)];
		Edited ();
	}

	void OnUrl (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		string u = url.Text;
		if (Util.StartsI (u.TrimStart (), "curl ") && App.PasteCurl (u))
			return;
		T.Req.Url = u;
		T.Req.Params = Postman.ParamsOfUrl (u, T.Req.Params);
		queries.SetRows (T.Req.Params);
		Edited ();
	}

	/// <summary>Les parametres changes : l'URL suit.</summary>
	void OnParams (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		var r = T.Req;
		string u = r.Url;
		int q = u.IndexOf ('?'), hash = u.IndexOf ('#');
		string tail = hash >= 0 ? u.Substring (hash) : "";
		string b = q >= 0 ? u.Substring (0, q) : hash >= 0 ? u.Substring (0, hash) : u;
		var on = r.Params.Where (k => k.On && !k.Blank).Select (k => k.Value.Length > 0 || u.Contains ('=') ? k.Key + "=" + k.Value : k.Key).ToList ();
		r.Url = b + (on.Count > 0 ? "?" + string.Join ("&", on) : "") + tail;
		_binding = true;
		url.Text = r.Url;
		_binding = false;
		Edited ();
	}

	void OnTable (object s, EventArgs e) => Edited ();
	void OnAuth (object s, EventArgs e) => Edited ();
	void OnSection (object s, EventArgs e) { if (!_binding && T != null) T.ReqSection = sections.SelectedIndex; }
	void OnSend (object s, EventArgs e) => App.Send ();
	void OnSave (object s, EventArgs e) => App.Save ();
	void OnCode (object s, EventArgs e) => App.Code ();

	void Toggle (KVTable k, Button b)
	{
		k.Bulk = !k.Bulk;
		b.Text = k.Bulk ? "Key-Value Edit" : "Bulk Edit";
	}
	void OnParamsBulk (object s, EventArgs e) => Toggle (queries, paramsBulk);
	void OnHeadersBulk (object s, EventArgs e) => Toggle (headers, headersBulk);
	void OnBodyBulk (object s, EventArgs e) => Toggle (T?.Req.BodyMode == BodyMode.Form ? form : urlenc, bodyBulk);

	void OnBodyMode (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.BodyMode = (BodyMode) bodyMode.SelectedIndex;
		BodyShown ();
		Edited ();
	}

	void OnRawLang (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.RawLang = (RawLang) rawLang.SelectedIndex;
		Edited ();
	}

	void OnRaw (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.Raw = raw.Text;
		Edited ();
	}

	public void Beautify ()
	{
		var r = T?.Req;
		if (r == null)
			return;
		string o = r.RawLang == RawLang.Json ? Pretty.Json (r.Raw) : r.RawLang == RawLang.Xml ? Pretty.Xml (r.Raw) : null;
		if (o == null)
		{
			App.Status ("The body could not be formatted (is it valid JSON or XML?).", true);
			return;
		}
		r.Raw = o;
		_binding = true;
		raw.Text = o;
		_binding = false;
		Edited ();
	}
	void OnBeautify (object s, EventArgs e) => Beautify ();

	void OnBinary (object s, EventArgs e)
	{
		if (T == null)
			return;
		using var d = new OpenFileDialog { InitialDirectory = "SD0:/" };
		if (d.ShowDialog () != DialogResult.OK)
			return;
		T.Req.Binary = d.FileName;
		binaryName.Text = d.FileName;
		Edited ();
	}

	void OnFollow (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.FollowRedirects = follow.Checked;
		Edited ();
	}

	void OnVerify (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		T.Req.Insecure = !verify.Checked;
		Edited ();
	}

	void OnTimeout (object s, EventArgs e)
	{
		if (_binding || T == null)
			return;
		int.TryParse (timeout.Text, out int ms);
		T.Req.TimeoutMs = Math.Max (0, ms);
		Edited ();
	}
}

partial class ResponseView : UserControl
{
	const int TextMax = 900000;		// un ui_textarea tient 1 Mio
	public MainForm App { get; set; }
	Tab T => App.Cur;

	/// <summary>Ce que l'onglet montre : rien, l'envoi en cours, l'erreur, ou la reponse.</summary>
	public void Shown_ (Tab t)
	{
		bool busy = t?.Job != null, has = t?.Resp != null && !busy, ok = has && t.Resp.Status >= 0;
		sending.Visible = busy;
		tabs.Visible = ok;
		empty.Visible = !busy && !ok;
		chip.Show_ (has ? t.Resp : null);
		emptyTitle.Text = !has || ok ? "Enter the URL and click Send to get a response" : t.Resp.Cancelled ? "The request was cancelled" : "Could not send the request";
		var lines = Wrap (!has || ok ? "Ctrl+Enter sends too; {{variables}} come from the environment chosen, top right." : t.Resp.Error, Math.Max (20, Width / 8));
		emptyLine1.Text = lines.Count > 0 ? lines[0] : "";
		emptyLine2.Text = lines.Count > 1 ? lines[1] : "";
		emptyLine3.Text = lines.Count > 2 ? string.Join (" ", lines.Skip (2)) : "";
	}

	/// <summary>Le texte en lignes d'au plus n caracteres : une etiquette d'UIKit ne coupe pas.</summary>
	static List<string> Wrap (string s, int n)
	{
		var o = new List<string> ();
		while (s.Length > n)
		{
			int at = s.LastIndexOf (' ', n);
			if (at <= 0)
				at = n;
			o.Add (s.Substring (0, at));
			s = s.Substring (at).TrimStart ();
		}
		o.Add (s);
		return o;
	}

	public void Elapsed (long ms) => sendingText.Text = "Sending request... " + ms / 1000 + "." + ms / 100 % 10 + " s";

	/// <summary>La reponse de l'onglet dans les pages.</summary>
	public void Fill (Tab t)
	{
		headers.Items.Clear ();
		cookies.Items.Clear ();
		results.Items.Clear ();
		testsSummary.Text = "";
		console.Text = "";
		if (t?.Resp != null)
		{
			var r = t.Resp;
			foreach (var h in r.Headers)
				headers.Items.Add (new ListViewItem (new[] { h.Key, h.Value }));
			foreach (var c in r.SetCookies)
				cookies.Items.Add (new ListViewItem (new[] { c.Name, c.Value, CookieText (c) }));
			foreach (var x in t.Results)
				results.Items.Add (new ListViewItem (new[] { x.Capture ? "SET" : x.Pass ? "PASS" : "FAIL", x.Name, x.Detail }));
			int pass = t.Results.Count (x => !x.Capture && x.Pass), all = t.Results.Count (x => !x.Capture), set = t.Results.Count - all;
			testsSummary.Text = t.Results.Count == 0 ? "No tests for this request: add them in its Tests tab." : pass + " / " + all + " passed" + (set > 0 ? ", " + set + " variable(s) set" : "");
			console.Text = Cut (r.Console.ToString ());
			view.SelectedIndex = t.BodyView;
			tabs.SelectedIndex = t.RespSection;
			BodyFill (t);
		}
		Shown_ (t);
	}

	public static string CookieText (Cookie c) => (c.HostOnly ? "" : ".") + c.Domain + "  path " + c.Path + (c.Secure ? "  secure" : "") + (c.HttpOnly ? "  HttpOnly" : "")
		+ (c.Expires != 0 ? "  expires " + Util.Iso (c.Expires) : "");

	static string Cut (string s) => s.Length > TextMax ? s.Substring (0, TextMax) + "\n... (cut here: Save keeps it all)\n" : s;

	static string Hex (byte[] b)
	{
		var o = new StringBuilder ();
		int m = Math.Min (b.Length, 64 * 1024);
		for (int i = 0; i < m; i += 16)
		{
			o.Append (i.ToString ("x8")).Append ("  ");
			for (int k = 0; k < 16; k++)
			{
				o.Append (i + k < m ? b[i + k].ToString ("x2") + " " : "   ");
				if (k == 7)
					o.Append (' ');
			}
			o.Append (' ');
			for (int k = 0; k < 16 && i + k < m; k++)
				o.Append (b[i + k] >= 32 && b[i + k] < 127 ? (char) b[i + k] : '.');
			o.Append ('\n');
		}
		if (b.Length > m)
			o.Append ("... (" + b.Length + " bytes in all)\n");
		return o.ToString ();
	}

	void BodyFill (Tab t)
	{
		var r = t.Resp;
		string ct = r.Header ("Content-Type") ?? "";
		int semi = ct.IndexOf (';');
		ctype.Text = semi >= 0 ? ct.Substring (0, semi) : ct;
		bool json = Util.HasI (ct, "json"), xml = Util.HasI (ct, "xml"), html = Util.HasI (ct, "html"), image = Util.HasI (ct, "image/");
		var b = r.Body;
		if (!json && !xml && !html && b.Length > 0)
		{
			string head = r.Text_.TrimStart ();
			json = head.StartsWith ('{') || head.StartsWith ('[');
			xml = head.StartsWith ('<');
		}
		bool picture = t.BodyView == 2 && image;
		var img = picture ? Image.FromBytes (b) : null;
		bodyImage.Visible = img != null;
		bodyText.Visible = img == null;
		if (img != null)
		{
			bodyImage.Image = img;
			return;
		}
		bool binary = b.Take (2048).Any (c => c < 8);
		string o = null;
		if (picture)
			o = "This image cannot be previewed (PNG and BMP can).\n";
		else if (binary)
			o = Hex (b);
		else if (t.BodyView == 0 && json)
			o = Pretty.Json (r.Text_);
		else if (t.BodyView == 0 && xml && !html)
			o = Pretty.Xml (r.Text_);
		else if (t.BodyView == 2 && html)
			o = Pretty.HtmlText (r.Text_);
		bodyText.Text = Cut (o ?? r.Text_);
	}

	void OnCancel (object s, EventArgs e) => App.CancelSend ();
	void OnTab (object s, EventArgs e) { if (T != null) T.RespSection = tabs.SelectedIndex; }

	void OnView (object s, EventArgs e)
	{
		if (T?.Resp == null || T.BodyView == view.SelectedIndex)
			return;
		T.BodyView = view.SelectedIndex;
		BodyFill (T);
	}

	void OnFind (object s, EventArgs e) { if (find.Text.Length > 0) bodyText.Find (find.Text); }

	void OnFindNext (object s, EventArgs e)
	{
		if (find.Text.Length > 0 && !bodyText.Find (find.Text))
			App.Status ("Not found.");
	}

	void OnCopyBody (object s, EventArgs e)
	{
		if (T?.Resp == null)
			return;
		Clipboard.SetText (bodyText.Text);
		App.Status ("The response's body is in the clipboard.");
	}

	void OnSaveBody (object s, EventArgs e)
	{
		var r = T?.Resp;
		if (r == null)
			return;
		string ct = r.Header ("Content-Type") ?? "";
		string ext = Util.HasI (ct, "json") ? "json" : Util.HasI (ct, "html") ? "html" : Util.HasI (ct, "xml") ? "xml" : Util.HasI (ct, "png") ? "png"
			: Util.HasI (ct, "jpeg") ? "jpg" : Util.HasI (ct, "gif") ? "gif" : "txt";
		using var d = new SaveFileDialog { InitialDirectory = "SD0:/", FileName = "response." + ext };
		if (d.ShowDialog () != DialogResult.OK)
			return;
		try
		{
			File.WriteAllBytes (d.FileName, r.Body);
			App.Status ("The response was saved: " + d.FileName);
		}
		catch (Exception x) when (x is IOException || x is UnauthorizedAccessException)
		{
			App.Status ("The response could not be written.", true);
		}
	}
}

partial class EnvView : UserControl
{
	public MainForm App { get; set; }
	bool _binding;
	Env Cur => App.TabEnv (App.Cur);

	public void Bind (Env e)
	{
		bool glob = e == App.Store.Globals;
		_binding = true;
		name.Text = e.Name;
		name.Enabled = !glob;
		active.Visible = !glob;
		bool on = !glob && App.Store.Active == e;
		active.Text = on ? "Active" : "Set Active";
		help.Text = glob ? "The globals: seen by every request, whatever the environment (an environment's variable of the same name wins)."
			: "Use its variables as {{name}} in the URLs, the params, the headers, the bodies and the auth, while it is the active one.";
		vars.SetRows (e.Vars);
		_binding = false;
	}

	void OnName (object s, EventArgs e)
	{
		var c = Cur;
		if (_binding || c == null || c == App.Store.Globals || name.Text.Length == 0)
			return;
		c.Name = name.Text;
		App.EnvChanged (c);
		App.RefreshAll ();
	}

	void OnVars (object s, EventArgs e) { if (!_binding) App.EnvChanged (Cur); }

	void OnActive (object s, EventArgs e)
	{
		var c = Cur;
		if (c == null || c == App.Store.Globals)
			return;
		int i = App.Store.Envs.IndexOf (c);
		App.SetEnv (i == App.Store.ActiveEnv ? -1 : i);
		Bind (c);
	}

	void OnExport (object s, EventArgs e)
	{
		var c = Cur;
		if (c != null)
			App.ExportEnv (c == App.Store.Globals ? -1 : App.Store.Envs.IndexOf (c));
	}
}

partial class CollView : UserControl
{
	public MainForm App { get; set; }
	bool _binding;
	Collection Cur => App.TabColl (App.Cur);

	static int Count (Item it, ref int folders)
	{
		int n = 0;
		foreach (var k in it.Kids)
		{
			if (k.Folder)
			{
				folders++;
				n += Count (k, ref folders);
			}
			else
				n++;
		}
		return n;
	}

	public void Bind (Collection c)
	{
		_binding = true;
		name.Text = c.Name;
		desc.Text = c.Desc;
		auth.Bind (c.Auth);
		vars.SetRows (c.Vars);
		int f = 0, n = Count (c.Root, ref f);
		summary.Text = n + " request(s) in " + f + " folder(s)   |   Auth: " + (c.Auth.Type == AuthType.None ? "no auth" : Auth.Names[(int) c.Auth.Type]);
		_binding = false;
	}

	void Changed_ () { if (!_binding) App.CollectionChanged (Cur); }

	void OnName (object s, EventArgs e)
	{
		var c = Cur;
		if (_binding || c == null || name.Text.Length == 0)
			return;
		c.Name = name.Text;
		App.CollectionChanged (c);
		App.RefreshAll ();
	}

	void OnDesc (object s, EventArgs e)
	{
		if (_binding || Cur == null)
			return;
		Cur.Desc = desc.Text;
		Changed_ ();
	}

	void OnAuth (object s, EventArgs e) => Changed_ ();
	void OnVars (object s, EventArgs e) => Changed_ ();
	void OnAddRequest (object s, EventArgs e) { if (Cur != null) App.NewRequest (Cur.Root); }
	void OnExport (object s, EventArgs e) { if (Cur != null) App.ExportColl (Cur); }
}

partial class NameDialog : Form
{
	public NameDialog (string title, string prompt, string initial, string okText)
	{
		InitializeComponent ();
		Text = title;
		label.Text = prompt;
		value.Text = initial;
		ok.Text = okText;
		value.Focus ();
	}

	void OnEnter (object s, EventArgs e)
	{
		DialogResult = DialogResult.OK;
		Close ();
	}

	/// <summary>Le nom saisi ; null : abandon, ou vide.</summary>
	public static string Ask (string title, string prompt, string initial, string okText = "OK")
	{
		var d = new NameDialog (title, prompt, initial, okText);
		if (d.ShowDialog () != DialogResult.OK)
			return null;
		string t = d.value.Text.Trim ();
		return t.Length == 0 ? null : t;
	}
}

partial class SaveDialog : Form
{
	public sealed class Dest { public Item Folder; public Collection Coll; }
	readonly List<Dest> _dests = new ();
	readonly MainForm _app;
	public string Chosen => name.Text.Trim ();
	public Dest Where => dests.SelectedIndex >= 0 && dests.SelectedIndex < _dests.Count ? _dests[dests.SelectedIndex] : null;

	public SaveDialog (MainForm app, string initial, Item preselect)
	{
		InitializeComponent ();
		_app = app;
		name.Text = initial;
		Fill ();
		int i = _dests.FindIndex (d => d.Folder == preselect);
		if (_dests.Count > 0)
			dests.SelectedIndex = Math.Max (0, i);
	}

	void Folders (Item f, int depth, Collection c)
	{
		foreach (var k in f.Kids.Where (x => x.Folder))
		{
			_dests.Add (new Dest { Folder = k, Coll = c });
			dests.Items.Add (new string (' ', depth * 4) + k.Name);
			Folders (k, depth + 1, c);
		}
	}

	void Fill ()
	{
		_dests.Clear ();
		dests.Items.Clear ();
		foreach (var c in _app.Store.Colls)
		{
			_dests.Add (new Dest { Folder = c.Root, Coll = c });
			dests.Items.Add (c.Name);
			Folders (c.Root, 1, c);
		}
	}

	void OnNewCollection (object s, EventArgs e)
	{
		int before = _app.Store.Colls.Count;
		_app.NewCollection ();
		Fill ();
		if (_app.Store.Colls.Count > before)
			dests.SelectedIndex = _dests.FindIndex (d => d.Coll == _app.Store.Colls[^1]);
	}
}

partial class CodeDialog : Form
{
	static int s_lang;
	readonly Request _req;
	readonly Scope _scope;
	readonly Auth _auth;

	public CodeDialog (Request r, Scope sc, Auth a)
	{
		InitializeComponent ();
		_req = r;
		_scope = sc;
		_auth = a;
		lang.SelectedIndex = s_lang;
		Fill ();
	}

	void Fill () => code.Text = Snippet.Make (s_lang, _req, _scope, _auth);

	void OnLang (object s, EventArgs e)
	{
		s_lang = lang.SelectedIndex;
		Fill ();
	}

	void OnCopy (object s, EventArgs e) => Clipboard.SetText (code.Text);
}

partial class ImportDialog : Form
{
	public string Path_ = "";
	public string Pasted => text.Text;

	void OnFile (object s, EventArgs e)
	{
		using var d = new OpenFileDialog { InitialDirectory = "SD0:/", Filter = "JSON files|*.json|All files|*" };
		if (d.ShowDialog () != DialogResult.OK)
			return;
		Path_ = d.FileName;
		DialogResult = DialogResult.Retry;
		Close ();
	}
}

partial class CookiesDialog : Form
{
	readonly MainForm _app;

	public CookiesDialog (MainForm app)
	{
		InitializeComponent ();
		_app = app;
		Fill ();
	}

	void Fill ()
	{
		list.Items.Clear ();
		foreach (var c in _app.Jar.List)
			list.Items.Add (new ListViewItem (new[] { c.Name, c.Value, ResponseView.CookieText (c) }));
		count.Text = _app.Jar.List.Count + " cookie(s) kept, sent to their domains";
	}

	void OnDeleteAll (object s, EventArgs e)
	{
		_app.Jar.List.Clear ();
		_app.Jar.Dirty = true;
		Fill ();
		_app.Status ("The cookies were deleted.");
	}
}
