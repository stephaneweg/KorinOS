// MainForm.cs -- Courier, le client HTTP d'Onyx a la Postman, ecrit en C# sur System.Windows.Forms : requetes
// (methodes, en-tetes, authentification, corps, {{variables}}), collections, environnements, historique, cookies,
// reponse mise en forme, tests et captures, code et cURL, import et export Postman. Voir docs/DOTNET.md.
//     /apps/courier.app/main [fichier a importer]
// Les morceaux : Model.cs (donnees, formats de Postman), Net.cs (moteur HTTP), Tools.cs (tests, code, cURL, mise
// en forme), KVTable.cs, SideList.cs (controles), Views.cs (le code des .ui), ici les onglets et les commandes.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Forms;

enum TabKind { Request, Env, Coll }

/// <summary>Un onglet : une copie de travail de requete, un environnement ou une collection (edites sur place).</summary>
class Tab
{
	public TabKind Kind;
	public Request Req = new ();
	public string ItemId = "", EnvId = "", CollId = "";
	public bool Dirty;
	public Response Resp;
	public Job Job;
	public DateTime Started;
	public List<TestResult> Results = new ();
	public int ReqSection, RespSection, BodyView, Split = -1;
}

partial class MainForm : Form
{
	const string JsonKinds = "JSON files|*.json|All files|*";
	public readonly Store Store = new ();
	public CookieJar Jar = new ();
	readonly List<Tab> _tabs = new ();
	readonly Scope _scope = new ();
	readonly Timer _tick = new () { Interval = 200 };
	readonly string[] _args;
	int _cur = -1, _ticks;
	long _saveAt;
	bool _stateDirty, _syncing;
	string _statusMsg = "";
	long _statusAt;

	public Tab Cur => _cur >= 0 && _cur < _tabs.Count ? _tabs[_cur] : null;

	public MainForm (string[] args)
	{
		_args = args;
		InitializeComponent ();
		side.App = this;
		reqView.App = this;
		reqView.Response.App = this;
		envView.App = this;
		collView.App = this;
		MainMenuStrip = BuildMenu ();
		Load += OnStart;
		Shown += OnShown;
		_tick.Tick += OnTick;
	}

	static void Main (string[] args) => Application.Run (new MainForm (args));

	MenuStrip BuildMenu ()
	{
		var m = new ToolStripMenuItem ("File");
		void Add (string text, string key, Action a)
		{
			var i = new ToolStripMenuItem (text) { ShortcutKeyDisplayString = key };
			i.Click += (s, e) => a ();
			m.DropDownItems.Add (i);
		}
		void Line () => m.DropDownItems.Add (new ToolStripMenuItem ("-"));
		Add ("New Request", "Ctrl+N", () => NewRequest (null));
		Add ("New Collection", "", NewCollection);
		Add ("New Environment", "", NewEnv);
		Line ();
		Add ("Import...", "Ctrl+O", Import);
		Add ("Export Collection...", "", ExportCurrent);
		Line ();
		Add ("Save", "Ctrl+S", Save);
		Add ("Save As...", "", SaveAs);
		Add ("Close Tab", "Ctrl+W", () => CloseTab (_cur));
		Line ();
		Add ("Send", "F5", Send);
		Add ("Code...", "", Code);
		Add ("Beautify the Body", "Ctrl+B", reqView.Beautify);
		Add ("Next Tab", "", () => { if (_tabs.Count > 0) SelectTab ((_cur + 1) % _tabs.Count); });
		Add ("Previous Tab", "", () => { if (_tabs.Count > 0) SelectTab ((_cur + _tabs.Count - 1) % _tabs.Count); });
		Line ();
		Add ("Collections", "", () => side.SetMode (0));
		Add ("Environments", "", () => side.SetMode (1));
		Add ("History", "Ctrl+H", () => side.SetMode (2));
		Add ("Cookies...", "", ShowCookies);
		Add ("Clear the History...", "", ClearHistory);
		var strip = new MenuStrip ();
		strip.Items.Add (m);
		return strip;
	}

	void OnStart (object s, EventArgs e)
	{
		Store.Load ();
		LoadCookies ();
		side.SetMode (0);
		LoadState ();
		RefreshAll ();
		ShowViews ();
		if (_tabs.Count == 0)
			Status ("Welcome. New (Ctrl+N) makes a request; Import (Ctrl+O) reads Postman's collections.");
		_tick.Start ();
	}

	/// <summary>Le fichier de la ligne de commande : une fois la fenetre a sa taille, l'import peut ouvrir un dialogue.</summary>
	void OnShown (object s, EventArgs e)
	{
		if (_args.Length > 0)
			ImportFile (_args[0]);
	}

	// ---- la barre d'etat --------------------------------------------------------------------------------------
	public void Status (string msg, bool error = false)
	{
		_statusMsg = msg;
		_statusAt = error ? long.MaxValue : Environment.TickCount64 + 8000;
		int room = Math.Max (20, status.Width / 7);		// (une etiquette d'UIKit ne coupe pas)
		status.Text = (error ? "! " : "") + (msg.Length > room ? msg.Substring (0, room - 3) + "..." : msg);
	}

	void StatusRight () => statusRight.Text = (Store.Active?.Name ?? "No environment") + "   |   " + Jar.List.Count + " cookie(s)";

	void OnTick (object s, EventArgs e)
	{
		var t = Cur;
		if (t?.Job != null)
			reqView.Response.Elapsed ((long) (DateTime.UtcNow - t.Started).TotalMilliseconds);
		if (++_ticks % 5 == 0)
		{
			Flush (false);
			if (_statusMsg.Length > 0 && Environment.TickCount64 > _statusAt)
			{
				_statusMsg = "";
				status.Text = "";
			}
		}
	}

	// ---- les onglets ------------------------------------------------------------------------------------------
	string TabTitle (Tab t)
	{
		if (t.Kind == TabKind.Env)
			return TabEnv (t)?.Name ?? "?";
		if (t.Kind == TabKind.Coll)
			return TabColl (t)?.Name ?? "?";
		return (t.Dirty ? "* " : "") + t.Req.Method + " " + (t.Req.Name.Length == 0 ? "Untitled Request" : t.Req.Name);
	}

	void TitlesUpdate ()
	{
		for (int i = 0; i < _tabs.Count; i++)
			docTabs.SetTitle (i, TabTitle (_tabs[i]));
	}

	public Item TabItem (Tab t, out Collection c)
	{
		c = null;
		return t != null && t.Kind == TabKind.Request ? Store.FindItem (t.ItemId, out c) : null;
	}

	public Env TabEnv (Tab t)
	{
		if (t == null || t.Kind != TabKind.Env)
			return null;
		return t.EnvId == "globals" ? Store.Globals : Store.Envs.FirstOrDefault (e => e.Id == t.EnvId);
	}

	public Collection TabColl (Tab t)
	{
		if (t == null)
			return null;
		if (t.Kind == TabKind.Coll)
			return Store.Colls.FirstOrDefault (c => c.Id == t.CollId);
		TabItem (t, out var coll);
		return coll;
	}

	void UpdateScope ()
	{
		_scope.Env = Store.Active;
		_scope.Globals = Store.Globals;
		_scope.Coll = TabColl (Cur);
	}

	void ShowViews ()
	{
		var t = Cur;
		welcome.Visible = t == null;
		reqView.Visible = t?.Kind == TabKind.Request;
		envView.Visible = t?.Kind == TabKind.Env;
		collView.Visible = t?.Kind == TabKind.Coll;
	}

	void SelectTab (int i)
	{
		if (Cur?.Kind == TabKind.Request)
			Cur.Split = reqView.Split;
		_cur = i;
		UpdateScope ();
		var t = Cur;
		if (t?.Kind == TabKind.Request)
			reqView.Bind (t);
		else if (t?.Kind == TabKind.Env && TabEnv (t) != null)
			envView.Bind (TabEnv (t));
		else if (t?.Kind == TabKind.Coll && TabColl (t) != null)
			collView.Bind (TabColl (t));
		if (t?.Kind == TabKind.Request)
			reqView.Response.Fill (t);
		ShowViews ();
		_syncing = true;
		if (i >= 0)
			docTabs.SelectedIndex = i;
		_syncing = false;
		side.Rebuild ();
		StatusRight ();
		_stateDirty = true;
	}

	void AddTab (Tab t)
	{
		_tabs.Add (t);
		_syncing = true;
		docTabs.Add (TabTitle (t));
		_syncing = false;
		SelectTab (_tabs.Count - 1);
	}

	public void CloseTab (int i)
	{
		if (i < 0 || i >= _tabs.Count)
			return;
		var t = _tabs[i];
		if (t.Kind == TabKind.Request && t.Dirty
			&& MessageBox.Show ("\"" + TabTitle (t).TrimStart ('*', ' ') + "\" has changes not saved. Close it anyway?", "Close Tab", MessageBoxButtons.OKCancel) != DialogResult.OK)
			return;
		t.Job?.Cancel ();
		t.Job = null;
		_tabs.RemoveAt (i);
		_syncing = true;
		docTabs.RemoveAt (i);
		_syncing = false;
		_cur = -1;
		SelectTab (_tabs.Count > 0 ? Math.Min (i, _tabs.Count - 1) : -1);
	}

	int FindTab (TabKind kind, string id) => _tabs.FindIndex (t => t.Kind == kind && (kind == TabKind.Request ? t.ItemId : kind == TabKind.Env ? t.EnvId : t.CollId) == id);

	void OnDocSelect (object s, EventArgs e)
	{
		if (!_syncing && docTabs.SelectedIndex != _cur)
			SelectTab (docTabs.SelectedIndex);
	}

	void OnDocClose (object s, int i) => CloseTab (i);

	/// <summary>Le modele a change : la barre laterale, les titres et le choix d'environnement suivent.</summary>
	public void RefreshAll ()
	{
		UpdateScope ();
		side.Rebuild ();
		RefreshEnvPick ();
		TitlesUpdate ();
		if (Cur?.Kind == TabKind.Request)
			reqView.SetAuthHelp (InheritText (Cur));
		StatusRight ();
		_stateDirty = true;
	}

	void RefreshEnvPick ()
	{
		_syncing = true;
		envPick.Items.Clear ();
		envPick.Items.Add ("No Environment");
		foreach (var e in Store.Envs)
			envPick.Items.Add (e.Name);
		envPick.SelectedIndex = Store.ActiveEnv + 1;
		_syncing = false;
	}

	void OnEnvPick (object s, EventArgs e)
	{
		if (!_syncing && envPick.SelectedIndex - 1 != Store.ActiveEnv)
			SetEnv (envPick.SelectedIndex - 1);
	}

	/// <summary>La requete de l'onglet a change.</summary>
	public void Edited ()
	{
		if (Cur == null)
			return;
		Cur.Dirty = true;
		TitlesUpdate ();
		_stateDirty = true;
		_saveAt = Environment.TickCount64;
	}

	public void CollectionChanged (Collection c)
	{
		if (c != null)
			c.Dirty = true;
		_saveAt = Environment.TickCount64;
	}

	public void EnvChanged (Env e)
	{
		if (e != null)
			e.Dirty = true;
		_saveAt = Environment.TickCount64;
	}

	// ---- ouvrir ------------------------------------------------------------------------------------------------
	public void OpenItem (Item it)
	{
		if (it == null || it.Folder)
			return;
		int i = FindTab (TabKind.Request, it.Id);
		if (i >= 0)
		{
			SelectTab (i);
			return;
		}
		var t = new Tab { Req = it.Req.Copy (), ItemId = it.Id };
		t.Req.Name = it.Name;
		AddTab (t);
	}

	public void OpenColl (Collection c)
	{
		int i = FindTab (TabKind.Coll, c.Id);
		if (i >= 0)
			SelectTab (i);
		else
			AddTab (new Tab { Kind = TabKind.Coll, CollId = c.Id });
	}

	public void OpenEnv (int idx)
	{
		string id = idx < 0 ? "globals" : Store.Envs[idx].Id;
		int i = FindTab (TabKind.Env, id);
		if (i >= 0)
			SelectTab (i);
		else
			AddTab (new Tab { Kind = TabKind.Env, EnvId = id });
	}

	public void OpenHistory (int idx)
	{
		var t = new Tab { Req = Store.History[idx].Req.Copy () };
		if (t.Req.Name.Length == 0)
			t.Req.Name = t.Req.Url;
		AddTab (t);
	}

	public void NewRequest (Item folder)
	{
		if (folder != null)
		{
			string nm = NameDialog.Ask ("New Request", "The request's name", "New Request", "Add");
			if (nm == null)
				return;
			var it = new Item { Id = Util.Uuid (), Name = nm };
			it.Req.Name = nm;
			folder.Add (it);
			folder.Open = true;
			var c = Store.CollectionOf (folder);
			if (c != null)
			{
				c.Root.Open = true;
				Store.Save (c);
			}
			RefreshAll ();
			OpenItem (it);
			return;
		}
		var t = new Tab ();
		t.Req.Name = "Untitled Request";
		AddTab (t);
		reqView.FocusUrl ();
	}

	public void NewFolder (Item parent)
	{
		string nm = NameDialog.Ask ("New Folder", "The folder's name", "New Folder", "Add");
		if (nm == null)
			return;
		parent.Add (new Item { Id = Util.Uuid (), Name = nm, Folder = true, Open = true });
		parent.Open = true;
		var c = Store.CollectionOf (parent);
		if (c != null)
		{
			c.Root.Open = true;
			Store.Save (c);
		}
		RefreshAll ();
	}

	public void NewCollection ()
	{
		string nm = NameDialog.Ask ("New Collection", "The collection's name", "New Collection", "Create");
		if (nm == null)
			return;
		var c = new Collection { Id = Util.Uuid (), Name = nm };
		Store.Colls.Add (c);
		Store.Save (c);
		side.SetMode (0);
		RefreshAll ();
		Status ("The collection was made: save requests in it (Ctrl+S).");
	}

	public void NewEnv ()
	{
		string nm = NameDialog.Ask ("New Environment", "The environment's name (e.g. Local, Staging, Production)", "New Environment", "Create");
		if (nm == null)
			return;
		var e = new Env { Id = Util.Uuid (), Name = nm };
		e.Vars.Add (new KV ("baseUrl", "http://localhost:8080"));
		Store.Envs.Add (e);
		Store.Save (e);
		side.SetMode (1);
		RefreshAll ();
		OpenEnv (Store.Envs.Count - 1);
	}

	void OnNew (object s, EventArgs e) => NewRequest (null);
	void OnNewCollection (object s, EventArgs e) => NewCollection ();
	void OnImport (object s, EventArgs e) => Import ();

	void OnGear (object s, EventArgs e)
	{
		var m = new ContextMenuStrip ();
		m.Add ("Cookies...", ShowCookies);
		m.Add ("Manage Environments", () => side.SetMode (1));
		m.Add ("-");
		m.Add ("Clear the History...", ClearHistory);
		m.Show (gear, new System.Drawing.Point (-150, gear.Height + 2));
	}

	// ---- les elements d'une collection ---------------------------------------------------------------------------
	void SaveOf (Item it)
	{
		var c = Store.CollectionOf (it);
		if (c != null)
			Store.Save (c);
	}

	public void RenameItem (Item it)
	{
		string nm = NameDialog.Ask (it.Folder ? "Rename Folder" : "Rename Request", "The new name", it.Name, "Rename");
		if (nm == null)
			return;
		it.Name = nm;
		it.Req.Name = nm;
		int i = FindTab (TabKind.Request, it.Id);
		if (i >= 0)
		{
			_tabs[i].Req.Name = nm;
			if (i == _cur)
				reqView.SetName (nm);
		}
		SaveOf (it);
		RefreshAll ();
	}

	public void DuplicateItem (Item it)
	{
		var p = it.Parent;
		if (p == null)
			return;
		var d = it.Clone ();
		d.Name += " Copy";
		d.Req.Name = d.Name;
		p.Add (d, p.Kids.IndexOf (it) + 1);
		SaveOf (p);
		RefreshAll ();
	}

	/// <summary>Les onglets d'un element sorti d'une collection : gardes, plus enregistres.</summary>
	void Detach (Item it)
	{
		foreach (var t in _tabs.Where (t => t.Kind == TabKind.Request && t.ItemId == it.Id))
		{
			t.ItemId = "";
			t.Dirty = true;
		}
		foreach (var k in it.Kids)
			Detach (k);
	}

	public void DeleteItem (Item it)
	{
		if (MessageBox.Show ("Delete \"" + it.Name + "\"" + (it.Folder ? " and everything in it" : "") + "? This cannot be undone.", it.Folder ? "Delete Folder" : "Delete Request", MessageBoxButtons.OKCancel) != DialogResult.OK)
			return;
		var c = Store.CollectionOf (it);
		Detach (it);
		it.Parent?.Kids.Remove (it);
		it.Parent = null;
		if (c != null)
			Store.Save (c);
		RefreshAll ();
	}

	public void MoveItem (Item it, int dir)
	{
		var p = it.Parent;
		if (p == null)
			return;
		int i = p.Kids.IndexOf (it), j = i + dir;
		if (j < 0 || j >= p.Kids.Count)
			return;
		p.Kids.RemoveAt (i);
		p.Kids.Insert (j, it);
		SaveOf (p);
		RefreshAll ();
	}

	public void RenameColl (Collection c)
	{
		string nm = NameDialog.Ask ("Rename Collection", "The new name", c.Name, "Rename");
		if (nm == null)
			return;
		c.Name = nm;
		Store.Save (c);
		if (Cur?.Kind == TabKind.Coll && TabColl (Cur) == c)
			collView.Bind (c);
		RefreshAll ();
	}

	public void DuplicateColl (Collection c)
	{
		using var doc = JsonDocument.Parse (Json.Make (w => Postman.WriteCollection (w, c)));
		var d = Postman.ReadCollection (doc.RootElement);
		d.Id = Util.Uuid ();
		d.Name += " Copy";
		Store.Colls.Add (d);
		Store.Save (d);
		RefreshAll ();
	}

	static void TryDelete (string path)
	{
		try
		{
			if (path.Length > 0)
				File.Delete (path);
		}
		catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
	}

	public void DeleteColl (Collection c)
	{
		if (MessageBox.Show ("Delete the collection \"" + c.Name + "\"? This cannot be undone (export it first to keep a copy).", "Delete Collection", MessageBoxButtons.OKCancel) != DialogResult.OK)
			return;
		Detach (c.Root);
		for (int i = _tabs.Count - 1; i >= 0; i--)
			if (_tabs[i].Kind == TabKind.Coll && _tabs[i].CollId == c.Id)
				CloseTab (i);
		TryDelete (c.File);
		Store.Colls.Remove (c);
		RefreshAll ();
	}

	void ExportJson (string name, string done, Action<Utf8JsonWriter> write)
	{
		using var d = new SaveFileDialog { InitialDirectory = "SD0:/", FileName = name, Filter = JsonKinds };
		if (d.ShowDialog () != DialogResult.OK)
			return;
		if (Store.Write (d.FileName, write))
			Status ("Exported to " + d.FileName + done);
		else
			Status ("The file could not be written.", true);
	}

	public void ExportColl (Collection c) => ExportJson (Util.SafeName (c.Name) + ".postman_collection.json", " (Postman collection v2.1).", w => Postman.WriteCollection (w, c));

	void ExportCurrent ()
	{
		var c = TabColl (Cur) ?? (Store.Colls.Count == 1 ? Store.Colls[0] : null);
		if (c == null)
			Status ("Open a request of the collection (or the collection) to export it; or use its menu in the sidebar.", true);
		else
			ExportColl (c);
	}

	// ---- les environnements et l'historique --------------------------------------------------------------------
	public void SetEnv (int idx)
	{
		Store.ActiveEnv = idx;
		RefreshAll ();
		Status ("Environment: " + (idx >= 0 ? Store.Envs[idx].Name : "none"));
	}

	public void RenameEnv (int idx)
	{
		var e = Store.Envs[idx];
		string nm = NameDialog.Ask ("Rename Environment", "The new name", e.Name, "Rename");
		if (nm == null)
			return;
		e.Name = nm;
		Store.Save (e);
		RefreshAll ();
	}

	public void DuplicateEnv (int idx)
	{
		var o = Store.Envs[idx];
		var e = new Env { Id = Util.Uuid (), Name = o.Name + " Copy", Vars = o.Vars.Select (k => k.Copy ()).ToList () };
		Store.Envs.Add (e);
		Store.Save (e);
		RefreshAll ();
	}

	public void DeleteEnv (int idx)
	{
		var e = Store.Envs[idx];
		if (MessageBox.Show ("Delete the environment \"" + e.Name + "\" and its variables?", "Delete Environment", MessageBoxButtons.OKCancel) != DialogResult.OK)
			return;
		for (int i = _tabs.Count - 1; i >= 0; i--)
			if (_tabs[i].Kind == TabKind.Env && _tabs[i].EnvId == e.Id)
				CloseTab (i);
		TryDelete (e.File);
		if (Store.ActiveEnv == idx)
			Store.ActiveEnv = -1;
		else if (Store.ActiveEnv > idx)
			Store.ActiveEnv--;
		Store.Envs.RemoveAt (idx);
		RefreshAll ();
	}

	public void ExportEnv (int idx)
	{
		var e = idx < 0 ? Store.Globals : Store.Envs[idx];
		ExportJson (idx < 0 ? "globals.postman_globals.json" : Util.SafeName (e.Name) + ".postman_environment.json", ".",
			w => Postman.WriteEnvironment (w, e, idx < 0 ? "globals" : "environment"));
	}

	public void ClearHistory ()
	{
		if (Store.History.Count == 0 || MessageBox.Show ("Delete every request of the history?", "Clear History", MessageBoxButtons.OKCancel) != DialogResult.OK)
			return;
		Store.History.Clear ();
		Store.SaveHistory ();
		RefreshAll ();
	}

	public void DeleteHistory (int idx)
	{
		Store.History.RemoveAt (idx);
		Store.HistoryDirty = true;
		_saveAt = Environment.TickCount64;
		RefreshAll ();
	}

	// ---- envoyer -------------------------------------------------------------------------------------------------
	/// <summary>L'authentification qui vaut : la sienne, celle d'un dossier, ou celle de la collection.</summary>
	public Auth EffectiveAuth (Request r, Item it, Collection c)
	{
		if (r.Auth.Type != AuthType.Inherit)
			return r.Auth;
		for (var p = it?.Parent; p != null; p = p.Parent)
			if (p.Parent != null && p.Auth.Type != AuthType.Inherit)
				return p.Auth;
		return c?.Auth ?? new Auth { Type = AuthType.None };
	}

	public string InheritText (Tab t)
	{
		var it = TabItem (t, out var c);
		if (c == null)
			return "Not in a collection: it inherits nothing. Save it in a collection, or choose a type.";
		var a = EffectiveAuth (t.Req, it, c);
		string from = c.Name;
		for (var p = it?.Parent; p?.Parent != null; p = p.Parent)
			if (p.Auth.Type != AuthType.Inherit)
			{
				from = p.Name;
				break;
			}
		return "This request is using " + Auth.Names[(int) a.Type] + " from \"" + from + "\".";
	}

	public void Send ()
	{
		var t = Cur;
		if (t == null || t.Kind != TabKind.Request)
			return;
		if (t.Job != null)
		{
			CancelSend ();
			return;
		}
		var it = TabItem (t, out var c);
		UpdateScope ();
		var j = new Job { Jar = Jar.Copy (), Prep = Prepared.Make (t.Req, _scope, EffectiveAuth (t.Req, it, c), Jar) };
		t.Job = j;
		t.Started = DateTime.UtcNow;
		reqView.SendButton ();
		reqView.Response.Shown_ (t);
		Status ("Sending...");
		Task.Run (async () =>
		{
			await j.Run ();
			BeginInvoke (() => JobDone (t, j));
		});
	}

	public void CancelSend ()
	{
		var t = Cur;
		if (t?.Job == null)
			return;
		t.Job.Cancel ();
		Status ("Cancelling...");
	}

	void RunTests (Tab t)
	{
		t.Results.Clear ();
		if (t.Resp == null || t.Resp.Status < 0)
			return;
		using var ev = new Eval (t.Resp);
		foreach (var k in t.Req.Tests.Where (x => x.On && x.Key.Length > 0))
		{
			string exp = k.Value.Trim ();
			bool pass = ev.Test (k.Key, k.Value, out string detail);
			t.Results.Add (new TestResult { Name = k.Key + " " + (exp.Length == 0 ? "exists" : exp), Pass = pass, Detail = detail });
		}
		var target = Store.Active ?? Store.Globals;
		foreach (var k in t.Req.Captures.Where (x => x.On && x.Key.Length > 0))
		{
			string name = k.Key.Trim (), v = ev.Get (k.Value);
			var r = new TestResult { Capture = true, Name = name + "  <-  " + k.Value, Pass = v != null, Detail = "not found in the response" };
			if (v != null)
			{
				Scope.Set (target.Vars, name, v);
				target.Dirty = true;
				_saveAt = Environment.TickCount64;
				r.Detail = "= " + (v.Length > 60 ? v.Substring (0, 60) + "..." : v) + "  (" + target.Name + ")";
			}
			t.Results.Add (r);
		}
	}

	void JobDone (Tab t, Job j)
	{
		if (!_tabs.Contains (t) || t.Job != j)
			return;
		t.Job = null;
		t.Resp = j.Resp;
		if (!j.Resp.Cancelled)
		{
			Jar = j.Jar;
			Jar.Dirty = true;
		}
		RunTests (t);
		var h = new HistoryEntry { When = Util.Now, Status = Math.Sign (j.Resp.Status) < 0 ? -1 : j.Resp.Status, Ms = j.Resp.MsTotal, Req = t.Req.Copy () };
		h.Req.Events = "";
		Store.History.Insert (0, h);
		if (Store.History.Count > Store.HistoryMax)
			Store.History.RemoveAt (Store.History.Count - 1);
		Store.HistoryDirty = true;
		_saveAt = Environment.TickCount64;
		if (t == Cur)
		{
			reqView.Response.Fill (t);
			reqView.SendButton ();
		}
		if (side.Mode == 2)
			side.Rebuild ();
		StatusRight ();
		if (j.Resp.Status >= 0)
		{
			int fails = t.Results.Count (r => !r.Pass && !r.Capture);
			Status (t.Req.Method + " " + t.Req.Name + ": " + j.Resp.Status + " " + j.Resp.Reason + " in " + j.Resp.MsTotal + " ms" + (fails > 0 ? " -- " + fails + " test(s) failed" : ""), fails > 0);
		}
		else
			Status (j.Resp.Error, !j.Resp.Cancelled);
	}

	// ---- enregistrer ---------------------------------------------------------------------------------------------
	public void Save ()
	{
		var t = Cur;
		if (t == null)
			return;
		if (t.Kind != TabKind.Request)
		{
			Status ("Environments and collections are saved as you edit them.");
			return;
		}
		var it = TabItem (t, out var c);
		if (it == null)
		{
			SaveAs ();
			return;
		}
		it.Req = t.Req.Copy ();
		it.Name = t.Req.Name;
		if (Store.Save (c))
		{
			t.Dirty = false;
			Status ("Saved in " + c.Name + ".");
		}
		else
			Status ("The collection could not be saved.", true);
		RefreshAll ();
	}

	public void SaveAs ()
	{
		var t = Cur;
		if (t == null || t.Kind != TabKind.Request)
			return;
		var d = new SaveDialog (this, t.Req.Name.Length == 0 ? "Untitled Request" : t.Req.Name, TabItem (t, out _)?.Parent);
		if (d.ShowDialog () != DialogResult.OK)
			return;
		var where = d.Where;
		if (where == null)
		{
			Status ("Choose a collection or a folder to save the request to.", true);
			return;
		}
		string nm = d.Chosen.Length == 0 ? "Untitled Request" : d.Chosen;
		t.Req.Name = nm;
		var it = new Item { Id = Util.Uuid (), Name = nm, Req = t.Req.Copy () };
		where.Folder.Add (it);
		where.Folder.Open = true;
		where.Coll.Root.Open = true;
		t.ItemId = it.Id;
		if (Store.Save (where.Coll))
		{
			t.Dirty = false;
			Status ("Saved in " + where.Coll.Name + ".");
		}
		reqView.SetName (nm);
		side.SetMode (0);
		RefreshAll ();
	}

	/// <summary>Chaque changement va sur la carte, une seconde apres le dernier.</summary>
	void Flush (bool now)
	{
		if (!now && Environment.TickCount64 - _saveAt < 1000)
			return;
		foreach (var c in Store.Colls.Where (c => c.Dirty))
			Store.Save (c);
		foreach (var e in Store.Envs.Where (e => e.Dirty))
			Store.Save (e);
		if (Store.Globals.Dirty)
			Store.Save (Store.Globals);
		if (Store.HistoryDirty)
			Store.SaveHistory ();
		if (Jar.Dirty)
			SaveCookies ();
		if (_stateDirty)
			SaveState ();
	}

	void SaveCookies ()
	{
		Store.Dirs ();
		long t = Util.Now;
		Store.Write (Store.FileCookies, w =>			// (ceux de session finissent avec elle)
		{
			w.WriteStartArray ();
			foreach (var c in Jar.List.Where (c => c.Expires >= t))
			{
				w.WriteStartObject ();
				w.WriteString ("domain", c.Domain);
				w.WriteString ("path", c.Path);
				w.WriteString ("name", c.Name);
				w.WriteString ("value", c.Value);
				w.WriteNumber ("expires", c.Expires);
				w.WriteBoolean ("secure", c.Secure);
				w.WriteBoolean ("hostOnly", c.HostOnly);
				w.WriteBoolean ("httpOnly", c.HttpOnly);
				w.WriteEndObject ();
			}
			w.WriteEndArray ();
		}, false);
		Jar.Dirty = false;
	}

	void LoadCookies ()
	{
		using var d = Store.Parse (Store.FileCookies);
		if (d == null)
			return;
		foreach (var e in d.RootElement.Items ())
			Jar.List.Add (new Cookie
			{
				Domain = e.Get ("domain").Str (), Path = e.Get ("path").Str (), Name = e.Get ("name").Str (), Value = e.Get ("value").Str (),
				Expires = e.Get ("expires").Long (), Secure = e.Get ("secure").Bool (false), HostOnly = e.Get ("hostOnly").Bool (true), HttpOnly = e.Get ("httpOnly").Bool (false),
			});
	}

	void SaveState ()
	{
		if (Cur?.Kind == TabKind.Request)
			Cur.Split = reqView.Split;
		Store.Dirs ();
		Store.Write (Store.FileState, w =>
		{
			w.WriteStartObject ();
			w.WriteString ("environment", Store.Active?.Id ?? "");
			w.WriteNumber ("sidebar", side.Mode);
			w.WriteNumber ("sidebarWidth", hsplit.SplitterDistance);
			w.WriteNumber ("current", _cur);
			w.WritePropertyName ("tabs");
			w.WriteStartArray ();
			foreach (var t in _tabs)
			{
				w.WriteStartObject ();
				w.WriteNumber ("kind", (int) t.Kind);
				if (t.Kind == TabKind.Env)
					w.WriteString ("env", t.EnvId);
				else if (t.Kind == TabKind.Coll)
					w.WriteString ("coll", t.CollId);
				else
				{
					w.WriteString ("item", t.ItemId);
					w.WriteNumber ("section", t.ReqSection);
					if (t.Split > 0)
						w.WriteNumber ("split", t.Split);
					if (t.Dirty || t.ItemId.Length == 0)		// (sa copie de travail)
					{
						w.WriteBoolean ("dirty", t.Dirty);
						w.WritePropertyName ("request");
						Postman.WriteRequestAlone (w, t.Req);
					}
				}
				w.WriteEndObject ();
			}
			w.WriteEndArray ();
			w.WriteEndObject ();
		});
		_stateDirty = false;
	}

	void LoadState ()
	{
		using var d = Store.Parse (Store.FileState);
		if (d == null)
			return;
		var r = d.RootElement;
		string env = r.Get ("environment").Str ();
		Store.ActiveEnv = Store.Envs.FindIndex (e => e.Id == env);
		side.SetMode (Math.Clamp (r.Get ("sidebar").Int (), 0, 2));
		int sw = r.Get ("sidebarWidth").Int ();
		if (sw > 0)
			hsplit.SplitterDistance = sw;
		foreach (var e in r.Get ("tabs").Items ())
		{
			var t = new Tab { Kind = (TabKind) e.Get ("kind").Int () };
			if (t.Kind == TabKind.Env)
			{
				t.EnvId = e.Get ("env").Str ();
				if (t.EnvId != "globals" && !Store.Envs.Any (x => x.Id == t.EnvId))
					continue;
			}
			else if (t.Kind == TabKind.Coll)
			{
				t.CollId = e.Get ("coll").Str ();
				if (!Store.Colls.Any (x => x.Id == t.CollId))
					continue;
			}
			else
			{
				t.ItemId = e.Get ("item").Str ();
				t.ReqSection = Math.Clamp (e.Get ("section").Int (), 0, 5);
				t.Split = e.Get ("split").Int (-1);
				var it = Store.FindItem (t.ItemId, out _);
				if (it == null)
					t.ItemId = "";
				if (e.Get ("request").ValueKind == JsonValueKind.Object)
				{
					t.Req = Postman.ReadRequestAlone (e.Get ("request"));
					t.Dirty = e.Get ("dirty").Bool (false);
				}
				else if (it != null)
				{
					t.Req = it.Req.Copy ();
					t.Req.Name = it.Name;
				}
				else
					continue;
			}
			_tabs.Add (t);
			docTabs.Add (TabTitle (t));
		}
		if (_tabs.Count > 0)
			SelectTab (Math.Clamp (r.Get ("current").Int (), 0, _tabs.Count - 1));
	}

	// ---- importer, code, cookies -------------------------------------------------------------------------------
	static void Tell (string text) => MessageBox.Show (text, "Import");

	bool ImportText (string s, string from)
	{
		string t = s.Trim ();
		if (t.Length == 0)
			return false;
		if (Util.StartsI (t, "curl"))
		{
			var q = Curl.Parse (t, out string err);
			if (q == null)
			{
				Tell ("The cURL command could not be read: " + err);
				return false;
			}
			AddTab (new Tab { Req = q, Dirty = true });
			TitlesUpdate ();
			Status ("The cURL command is a new request: Save it in a collection (Ctrl+S).");
			return true;
		}
		JsonDocument d;
		try { d = JsonDocument.Parse (t, Json.Tolerant); }
		catch (JsonException e)
		{
			Tell (from + " is neither a cURL command nor JSON (" + e.Message + ").");
			return false;
		}
		using (d)
		{
			var r = d.RootElement;
			if (Postman.IsCollection (r))
			{
				var c = Postman.ReadCollection (r);
				if (Store.Colls.Any (x => x.Id == c.Id))
					c.Id = Util.Uuid ();			// (deja ici : une copie)
				Store.Colls.Add (c);
				Store.Save (c);
				side.SetMode (0);
				RefreshAll ();
				Status ("Imported the collection \"" + c.Name + "\".");
				return true;
			}
			if (r.Get ("values").ValueKind == JsonValueKind.Array)
			{
				var e = Postman.ReadEnvironment (r);
				if (r.Get ("_postman_variable_scope").Str () == "globals")
				{
					foreach (var k in e.Vars)
						Scope.Set (Store.Globals.Vars, k.Key, k.Value);
					Store.Save (Store.Globals);
					RefreshAll ();
					Status ("The globals were imported (merged).");
					return true;
				}
				if (Store.Envs.Any (x => x.Id == e.Id))
					e.Id = Util.Uuid ();
				Store.Envs.Add (e);
				Store.Save (e);
				side.SetMode (1);
				RefreshAll ();
				Status ("Imported the environment \"" + e.Name + "\" (" + e.Vars.Count + " variable(s)).");
				return true;
			}
		}
		Tell ("This JSON is neither a Postman collection (info, item) nor an environment (values).");
		return false;
	}

	void ImportFile (string path)
	{
		try { ImportText (File.ReadAllText (path), Util.BaseName (path)); }
		catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { Tell (path + " cannot be read."); }
	}

	void Import ()
	{
		var d = new ImportDialog ();
		var r = d.ShowDialog ();
		if (r == DialogResult.Retry)
			ImportFile (d.Path_);
		else if (r == DialogResult.OK)
			ImportText (d.Pasted, "The text");
	}

	/// <summary>Une commande cURL collee dans l'URL devient la requete de l'onglet.</summary>
	public bool PasteCurl (string text)
	{
		var t = Cur;
		var r = t == null ? null : Curl.Parse (text.Trim (), out _);
		if (r == null)
			return false;
		r.Name = t.Req.Name;
		r.Tests = t.Req.Tests;
		r.Captures = t.Req.Captures;
		t.Req = r;
		BeginInvoke (() =>			// (hors du rappel du champ, qu'on va remplir)
		{
			reqView.Bind (t);
			Edited ();
			Status ("The cURL command was read into the request.");
		});
		return true;
	}

	public void Code ()
	{
		var t = Cur;
		if (t == null || t.Kind != TabKind.Request)
			return;
		var it = TabItem (t, out var c);
		UpdateScope ();
		new CodeDialog (t.Req, _scope, EffectiveAuth (t.Req, it, c)).ShowDialog ();
	}

	void ShowCookies ()
	{
		new CookiesDialog (this).ShowDialog ();
		StatusRight ();
	}

	void OnKey (object s, KeyEventArgs e)
	{
		if (e.KeyCode == Keys.Enter && e.Control)
		{
			Send ();
			e.Handled = true;
		}
		else if (e.KeyCode == Keys.Escape && Cur?.Job != null)
		{
			CancelSend ();
			e.Handled = true;
		}
	}

	void OnClosing (object s, FormClosingEventArgs e)
	{
		_stateDirty = true;
		Flush (true);
	}
}
