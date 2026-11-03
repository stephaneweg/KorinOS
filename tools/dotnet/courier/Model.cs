// Model.cs -- les donnees de Courier : requetes, collections, environnements, globales, historique, et leurs
// fichiers, aux formats de Postman (collection v2.1, environnement) : une collection faite ici s'ouvre dans
// Postman, et le contraire. Les Tests et Captures sont gardes dans le membre « onyx » de l'item, le meme que le
// Courier d'Onyx, et ecrits aussi en JavaScript dans son evenement « test », pour que Postman les joue. Les
// scripts de Postman que Courier ne joue pas sont gardes tels quels et reecrits.
//     /home/courier/collections/<id>.postman_collection.json
//     /home/courier/environments/<id>.postman_environment.json
//     /home/courier/globals.json, history.json, state.json, cookies.json
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

class KV
{
	public string Key = "", Value = "", Desc = "";
	public bool On = true;
	public bool File;			// (form-data) Value est le chemin d'un fichier
	public KV () { }
	public KV (string key, string value, bool on = true) { Key = key; Value = value; On = on; }
	public bool Blank => Key.Length == 0 && Value.Length == 0 && Desc.Length == 0;
	public KV Copy () => (KV) MemberwiseClone ();
}

enum AuthType { Inherit, None, Bearer, Basic, ApiKey }

class Auth
{
	public AuthType Type = AuthType.Inherit;
	public string Token = "", User = "", Pass = "", Key = "", Value = "";
	public bool InQuery;
	public Auth Copy () => (Auth) MemberwiseClone ();
	public static readonly string[] Names = { "Inherit auth from parent", "No Auth", "Bearer Token", "Basic Auth", "API Key" };
}

enum BodyMode { None, Form, UrlEncoded, Raw, Binary }
enum RawLang { Text, Json, Xml, Html, JavaScript }

class Request
{
	public static readonly string[] Methods = { "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" };
	public static readonly string[] RawNames = { "text", "json", "xml", "html", "javascript" };
	public static readonly string[] RawTypes = { "text/plain", "application/json", "application/xml", "text/html", "application/javascript" };

	public string Name = "", Method = "GET", Url = "", Desc = "";
	public List<KV> Params = new (), Headers = new ();
	public Auth Auth = new ();
	public BodyMode BodyMode;
	public RawLang RawLang = RawLang.Json;
	public string Raw = "", Binary = "";
	public List<KV> Form = new (), UrlEnc = new ();
	public List<KV> Tests = new (), Captures = new ();	// Tests : Key le controle, Value l'attendu ; Captures : Key la variable, Value la source
	public bool FollowRedirects = true;
	public bool Insecure;			// certificat TLS non verifie
	public int TimeoutMs;			// 0 : celui de l'application
	public string Events = "";		// le JSON des evenements de Postman que Courier ne joue pas

	static List<KV> Copy (List<KV> l) => l.Select (k => k.Copy ()).ToList ();
	public Request Copy ()
	{
		var r = (Request) MemberwiseClone ();
		r.Params = Copy (Params); r.Headers = Copy (Headers); r.Form = Copy (Form); r.UrlEnc = Copy (UrlEnc);
		r.Tests = Copy (Tests); r.Captures = Copy (Captures); r.Auth = Auth.Copy ();
		return r;
	}
}

class Item
{
	public string Id = "", Name = "", Desc = "", Events = "";
	public bool Folder, Open;
	public Request Req = new ();
	public List<Item> Kids = new ();
	public Item Parent;
	public Auth Auth = new ();		// (un dossier)

	public void Add (Item k, int at = -1)
	{
		k.Parent = this;
		if (at < 0 || at >= Kids.Count)
			Kids.Add (k);
		else
			Kids.Insert (at, k);
	}

	public Item Clone ()
	{
		var c = new Item { Id = Util.Uuid (), Name = Name, Desc = Desc, Folder = Folder, Req = Req.Copy (), Events = Events, Auth = Auth.Copy () };
		foreach (var k in Kids)
			c.Add (k.Clone ());
		return c;
	}
}

class Collection
{
	public string Id = "", Name = "", Desc = "", File = "", Events = "";
	public List<KV> Vars = new ();
	public Auth Auth = new () { Type = AuthType.None };
	public Item Root = new () { Folder = true, Open = true };
	public bool Dirty;
}

class Env
{
	public string Id = "", Name = "", File = "";
	public List<KV> Vars = new ();
	public bool Dirty;
}

class HistoryEntry
{
	public long When;
	public int Status, Ms;
	public Request Req = new ();
}

/// <summary>Ce qui se relit dans du JSON, sans se soucier de son type.</summary>
static class Json
{
	public static readonly JsonDocumentOptions Tolerant = new () { AllowTrailingCommas = true, CommentHandling = JsonCommentHandling.Skip };

	public static JsonElement Get (this JsonElement e, string name) =>
		e.ValueKind == JsonValueKind.Object && e.TryGetProperty (name, out var v) ? v : default;

	public static IEnumerable<JsonElement> Items (this JsonElement e)
	{
		if (e.ValueKind == JsonValueKind.Array)
			foreach (var x in e.EnumerateArray ())
				yield return x;
	}

	/// <summary>Le texte d'une valeur ; une description { content, type } donne son contenu.</summary>
	public static string Str (this JsonElement e) => e.ValueKind switch
	{
		JsonValueKind.String => e.GetString (),
		JsonValueKind.Number => e.GetRawText (),
		JsonValueKind.True => "true",
		JsonValueKind.False => "false",
		JsonValueKind.Object => e.Get ("content").Str (),
		_ => "",
	};

	public static bool Bool (this JsonElement e, bool or) => e.ValueKind == JsonValueKind.True || (e.ValueKind != JsonValueKind.False && or);
	public static int Int (this JsonElement e, int or = 0) => e.ValueKind == JsonValueKind.Number && e.TryGetInt32 (out int v) ? v : or;
	public static long Long (this JsonElement e) => e.ValueKind == JsonValueKind.Number && e.TryGetInt64 (out long v) ? v : 0;

	public static Utf8JsonWriter Writer (Stream s, bool indented = true) =>
		new Utf8JsonWriter (s, new JsonWriterOptions { Indented = indented, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping, SkipValidation = true });

	public static byte[] Make (Action<Utf8JsonWriter> f, bool indented = true)
	{
		using var m = new MemoryStream ();
		using (var w = Writer (m, indented))
			f (w);
		return m.ToArray ();
	}
}

/// <summary>Les formats de Postman, dans les deux sens.</summary>
static class Postman
{
	public const string Schema21 = "https://schema.getpostman.com/json/collection/v2.1.0/collection.json";

	// ---- lire ----
	static List<KV> ReadKvs (JsonElement arr)
	{
		var o = new List<KV> ();
		foreach (var e in arr.Items ())
		{
			var kv = new KV { Key = e.Get ("key").Str (), Value = e.Get ("value").Str (), Desc = e.Get ("description").Str () };
			kv.On = !e.Get ("disabled").Bool (false);
			if (e.Get ("enabled").ValueKind != JsonValueKind.Undefined)
				kv.On = e.Get ("enabled").Bool (true);	// (les valeurs d'un environnement)
			if (e.Get ("type").Str () == "file")
			{
				kv.File = true;
				var src = e.Get ("src");
				kv.Value = src.ValueKind == JsonValueKind.Array ? src.Items ().FirstOrDefault ().Str () : src.Str ();
			}
			o.Add (kv);
		}
		return o;
	}

	static string Pick (JsonElement list, string key)
	{
		if (list.ValueKind == JsonValueKind.Object)
			return list.Get (key).Str ();			// (v2.0)
		foreach (var e in list.Items ())
			if (e.Get ("key").Str () == key)
				return e.Get ("value").Str ();
		return "";
	}

	static Auth ReadAuth (JsonElement v)
	{
		var a = new Auth ();
		if (v.ValueKind != JsonValueKind.Object)
			return a;
		switch (v.Get ("type").Str ())
		{
		case "bearer":
			a.Type = AuthType.Bearer;
			a.Token = Pick (v.Get ("bearer"), "token");
			break;
		case "basic":
			a.Type = AuthType.Basic;
			a.User = Pick (v.Get ("basic"), "username");
			a.Pass = Pick (v.Get ("basic"), "password");
			break;
		case "apikey":
			a.Type = AuthType.ApiKey;
			a.Key = Pick (v.Get ("apikey"), "key");
			a.Value = Pick (v.Get ("apikey"), "value");
			a.InQuery = Pick (v.Get ("apikey"), "in") == "query";
			break;
		default:
			a.Type = AuthType.None;				// (noauth, ou un genre que Courier ne fait pas)
			break;
		}
		return a;
	}

	/// <summary>Les evenements gardes : sauf « onyx-tests », refait depuis ses lignes.</summary>
	static string ReadEvents (JsonElement ev)
	{
		var kept = ev.Items ().Where (e => e.Get ("script").Get ("id").Str () != "onyx-tests").ToList ();
		if (kept.Count == 0)
			return "";
		return Encoding.UTF8.GetString (Json.Make (w =>
		{
			w.WriteStartArray ();
			foreach (var e in kept)
				e.WriteTo (w);
			w.WriteEndArray ();
		}, false));
	}

	/// <summary>Les parametres : la requete de l'URL, puis ceux qui sont desactives.</summary>
	public static List<KV> ParamsOfUrl (string url, List<KV> known)
	{
		var all = new List<KV> ();
		int q = url.IndexOf ('?');
		if (q >= 0)
		{
			int h = url.IndexOf ('#', q);
			string s = h >= 0 ? url.Substring (q + 1, h - q - 1) : url.Substring (q + 1);
			foreach (var part in s.Split ('&'))
			{
				if (part.Length == 0)
					continue;
				int eq = part.IndexOf ('=');
				var kv = eq < 0 ? new KV (part, "") : new KV (part.Substring (0, eq), part.Substring (eq + 1));
				kv.Desc = known.FirstOrDefault (k => k.On && k.Key == kv.Key)?.Desc ?? "";
				all.Add (kv);
			}
		}
		all.AddRange (known.Where (k => !k.On && !k.Blank));
		return all;
	}

	static void ReadRequest (Request r, JsonElement item)
	{
		var q = item.Get ("request");
		r.Name = item.Get ("name").Str ();
		if (q.ValueKind == JsonValueKind.String)		// (v2 : une requete peut etre son URL)
		{
			r.Url = q.GetString ();
			r.Params = ParamsOfUrl (r.Url, r.Params);
			r.Events = ReadEvents (item.Get ("event"));
			return;
		}
		r.Method = q.Get ("method").ValueKind == JsonValueKind.String ? q.Get ("method").GetString () : "GET";
		var u = q.Get ("url");
		if (u.ValueKind == JsonValueKind.String)
			r.Url = u.GetString ();
		else
		{
			r.Url = u.Get ("raw").Str ();
			r.Params = ReadKvs (u.Get ("query"));
		}
		r.Params = ParamsOfUrl (r.Url, r.Params);
		r.Desc = q.Get ("description").Str ();
		r.Headers = ReadKvs (q.Get ("header"));
		r.Auth = ReadAuth (q.Get ("auth"));
		var b = q.Get ("body");
		if (b.ValueKind == JsonValueKind.Object && !b.Get ("disabled").Bool (false))
			switch (b.Get ("mode").Str ())
			{
			case "raw":
				r.BodyMode = BodyMode.Raw;
				r.Raw = b.Get ("raw").Str ();
				int lang = Array.IndexOf (Request.RawNames, b.Get ("options").Get ("raw").Get ("language").Str ());
				r.RawLang = lang >= 0 ? (RawLang) lang : RawLang.Text;
				break;
			case "urlencoded":
				r.BodyMode = BodyMode.UrlEncoded;
				r.UrlEnc = ReadKvs (b.Get ("urlencoded"));
				break;
			case "formdata":
				r.BodyMode = BodyMode.Form;
				r.Form = ReadKvs (b.Get ("formdata"));
				break;
			case "file":
				r.BodyMode = BodyMode.Binary;
				r.Binary = b.Get ("file").Get ("src").Str ();
				break;
			case "graphql":					// (en JSON brut : { query, variables })
				r.BodyMode = BodyMode.Raw;
				r.RawLang = RawLang.Json;
				r.Raw = Encoding.UTF8.GetString (Json.Make (w =>
				{
					w.WriteStartObject ();
					w.WriteString ("query", b.Get ("graphql").Get ("query").Str ());
					w.WriteString ("variables", b.Get ("graphql").Get ("variables").Str ());
					w.WriteEndObject ();
				}));
				break;
			}
		r.FollowRedirects = item.Get ("protocolProfileBehavior").Get ("followRedirects").Bool (true);
		var own = item.Get ("onyx");
		r.TimeoutMs = own.Get ("timeout").Int ();
		r.Insecure = own.Get ("insecure").Bool (false);
		r.Tests = ReadKvs (own.Get ("tests"));
		r.Captures = ReadKvs (own.Get ("captures"));
		r.Events = ReadEvents (item.Get ("event"));
	}

	public static Request ReadRequestAlone (JsonElement item)
	{
		var r = new Request ();
		ReadRequest (r, item);
		return r;
	}

	static void ReadItems (Item parent, JsonElement arr)
	{
		foreach (var e in arr.Items ())
		{
			var it = new Item { Id = e.Get ("id").Str (), Name = e.Get ("name").Str () };
			if (it.Id.Length == 0)
				it.Id = Util.Uuid ();
			if (e.Get ("item").ValueKind != JsonValueKind.Undefined)
			{
				it.Folder = true;
				it.Desc = e.Get ("description").Str ();
				it.Auth = ReadAuth (e.Get ("auth"));
				it.Events = ReadEvents (e.Get ("event"));
				ReadItems (it, e.Get ("item"));
			}
			else
				ReadRequest (it.Req, e);
			if (it.Name.Length == 0)
				it.Name = it.Folder ? "New Folder" : "New Request";
			parent.Add (it);
		}
	}

	public static bool IsCollection (JsonElement r) => r.Get ("info").ValueKind == JsonValueKind.Object && r.Get ("item").ValueKind == JsonValueKind.Array;

	public static Collection ReadCollection (JsonElement r)
	{
		var c = new Collection { Id = r.Get ("info").Get ("_postman_id").Str (), Name = r.Get ("info").Get ("name").Str (), Desc = r.Get ("info").Get ("description").Str () };
		if (c.Id.Length == 0)
			c.Id = Util.Uuid ();
		if (c.Name.Length == 0)
			c.Name = "Collection";
		c.Vars = ReadKvs (r.Get ("variable"));
		c.Auth = ReadAuth (r.Get ("auth"));
		if (c.Auth.Type == AuthType.Inherit)
			c.Auth.Type = AuthType.None;
		c.Events = ReadEvents (r.Get ("event"));
		ReadItems (c.Root, r.Get ("item"));
		return c;
	}

	public static Env ReadEnvironment (JsonElement r)
	{
		var e = new Env { Id = r.Get ("id").Str (), Name = r.Get ("name").Str (), Vars = ReadKvs (r.Get ("values")) };
		if (e.Id.Length == 0)
			e.Id = Util.Uuid ();
		if (e.Name.Length == 0)
			e.Name = "Environment";
		return e;
	}

	// ---- ecrire ----
	static void WriteDesc (Utf8JsonWriter w, string d)
	{
		if (d.Length > 0)
			w.WriteString ("description", d);
	}

	static void WriteKvs (Utf8JsonWriter w, string name, List<KV> l, bool env = false, bool form = false)
	{
		w.WritePropertyName (name);
		w.WriteStartArray ();
		foreach (var kv in l)
		{
			if (kv.Blank)
				continue;
			w.WriteStartObject ();
			w.WriteString ("key", kv.Key);
			if (form && kv.File)
			{
				w.WriteString ("type", "file");
				w.WriteString ("src", kv.Value);
			}
			else
			{
				w.WriteString ("value", kv.Value);
				if (env)
				{
					w.WriteString ("type", "default");
					w.WriteBoolean ("enabled", kv.On);
				}
				else if (form)
					w.WriteString ("type", "text");
			}
			if (!env && !kv.On)
				w.WriteBoolean ("disabled", true);
			WriteDesc (w, kv.Desc);
			w.WriteEndObject ();
		}
		w.WriteEndArray ();
	}

	static void WritePairs (Utf8JsonWriter w, string name, params (string k, string v)[] pairs)
	{
		w.WritePropertyName (name);
		w.WriteStartArray ();
		foreach (var (k, v) in pairs)
		{
			w.WriteStartObject ();
			w.WriteString ("key", k);
			w.WriteString ("value", v);
			w.WriteString ("type", "string");
			w.WriteEndObject ();
		}
		w.WriteEndArray ();
	}

	static void WriteAuth (Utf8JsonWriter w, Auth a)
	{
		if (a.Type == AuthType.Inherit)
			return;
		w.WritePropertyName ("auth");
		w.WriteStartObject ();
		switch (a.Type)
		{
		case AuthType.None:
			w.WriteString ("type", "noauth");
			break;
		case AuthType.Bearer:
			w.WriteString ("type", "bearer");
			WritePairs (w, "bearer", ("token", a.Token));
			break;
		case AuthType.Basic:
			w.WriteString ("type", "basic");
			WritePairs (w, "basic", ("username", a.User), ("password", a.Pass));
			break;
		case AuthType.ApiKey:
			w.WriteString ("type", "apikey");
			WritePairs (w, "apikey", ("key", a.Key), ("value", a.Value), ("in", a.InQuery ? "query" : "header"));
			break;
		}
		w.WriteEndObject ();
	}

	/// <summary>L'URL comme Postman la garde : raw et ses morceaux.</summary>
	static void WriteUrl (Utf8JsonWriter w, Request r)
	{
		w.WritePropertyName ("url");
		w.WriteStartObject ();
		string s = r.Url;
		w.WriteString ("raw", s);
		int end = s.IndexOf ('?');
		if (end < 0)
			end = s.Length;
		int hash = s.IndexOf ('#');
		if (hash >= 0 && hash < end)
			end = hash;
		int p = 0, sep = s.IndexOf ("://", StringComparison.Ordinal);
		if (sep >= 0 && sep < end)
		{
			w.WriteString ("protocol", s.Substring (0, sep));
			p = sep + 3;
		}
		int he = s.IndexOf ('/', p);
		if (he < 0 || he > end)
			he = end;
		string host = s.Substring (p, he - p), port = "";
		int colon = host.LastIndexOf (':');
		if (colon >= 0 && host.IndexOf ("}}", colon, StringComparison.Ordinal) < 0)
		{
			port = host.Substring (colon + 1);
			host = host.Substring (0, colon);
		}
		w.WritePropertyName ("host");
		w.WriteStartArray ();
		if (host.StartsWith ("{{"))
			w.WriteStringValue (host);
		else
			foreach (var part in host.Split ('.', StringSplitOptions.RemoveEmptyEntries))
				w.WriteStringValue (part);
		w.WriteEndArray ();
		if (port.Length > 0)
			w.WriteString ("port", port);
		w.WritePropertyName ("path");
		w.WriteStartArray ();
		if (he < end)
			foreach (var part in s.Substring (he + 1, end - he - 1).Split ('/', StringSplitOptions.RemoveEmptyEntries))
				w.WriteStringValue (part);
		w.WriteEndArray ();
		if (r.Params.Any (k => !k.Blank))
			WriteKvs (w, "query", r.Params);
		w.WriteEndObject ();
	}

	/// <summary>Le JavaScript d'un controle, pour Postman (« json.a.b[0] » -> pm.response.json().a.b[0]).</summary>
	static string JsExpr (string src)
	{
		string t = src.Trim ();
		if (Util.EqI (t, "status") || Util.EqI (t, "code"))
			return "pm.response.code";
		if (Util.EqI (t, "time"))
			return "pm.response.responseTime";
		if (Util.EqI (t, "size"))
			return "pm.response.responseSize";
		if (Util.EqI (t, "body"))
			return "pm.response.text()";
		if (Util.StartsI (t, "header.") || Util.StartsI (t, "headers."))
			return "pm.response.headers.get(\"" + t.Substring (t.IndexOf ('.') + 1) + "\")";
		if (Util.StartsI (t, "cookie."))
			return "pm.cookies.get(\"" + t.Substring (7) + "\")";
		if (Util.StartsI (t, "json") || t.StartsWith ('$'))
		{
			var o = new StringBuilder ("pm.response.json()");
			string path = t.Substring (t[0] == '$' ? 1 : 4);
			for (int i = 0; i < path.Length; i++)
			{
				if (path[i] != '.')
				{
					o.Append (path[i]);
					continue;
				}
				int e = i + 1;
				while (e < path.Length && path[e] != '.' && path[e] != '[')
					e++;
				string k = path.Substring (i + 1, e - i - 1);
				bool ident = k.Length > 0 && k.Select ((c, n) => char.IsAsciiLetter (c) || c == '_' || c == '$' || (n > 0 && char.IsAsciiDigit (c))).All (b => b);
				o.Append (ident ? "." + k : "[\"" + k + "\"]");
				i = e - 1;
			}
			return o.ToString ();
		}
		return "\"" + t + "\"";
	}

	static string JsQuote (string s) => "\"" + s.Replace ("\\", "\\\\").Replace ("\"", "\\\"").Replace ("\n", "\\n") + "\"";

	static string JsTest (KV t)
	{
		string e = JsExpr (t.Key), exp = t.Value.Trim (), x = exp, op = "==";
		string name = t.Key + " " + (exp.Length == 0 ? "exists" : exp);
		string[] ops = { "!=", "<=", ">=", "<", ">", "=" };
		var found = ops.FirstOrDefault (x.StartsWith);
		if (found != null)
		{
			op = found == "=" ? "==" : found;
			x = x.Substring (found.Length);
		}
		else if (Util.StartsI (x, "contains "))
		{
			op = "contains";
			x = x.Substring (9);
		}
		else if (Util.StartsI (x, "matches "))
		{
			op = "matches";
			x = x.Substring (8);
		}
		x = x.TrimStart ();
		string check;
		if (exp.Length == 0 || Util.EqI (x, "exists"))
			check = "pm.expect(v).to.not.be.oneOf([undefined, null]);";
		else if (op == "contains")
			check = "pm.expect(String(v)).to.include(" + JsQuote (x) + ");";
		else if (op == "matches")
			check = "pm.expect(String(v)).to.match(new RegExp(" + JsQuote (x) + "));";
		else if (op[0] == '<' || op[0] == '>')
			check = "pm.expect(Number(v) " + op + " Number(" + JsQuote (x) + ")).to.be.true;";
		else
			check = "pm.expect(String(v) " + (op[0] == '!' ? "!==" : "===") + " " + JsQuote (x) + ").to.be.true;";
		return "pm.test(" + JsQuote (name) + ", function () { var v = " + e + "; " + check + " });";
	}

	static void WriteTestEvent (Utf8JsonWriter w, Request r)
	{
		w.WriteStartObject ();
		w.WriteString ("listen", "test");
		w.WritePropertyName ("script");
		w.WriteStartObject ();
		w.WriteString ("id", "onyx-tests");
		w.WriteString ("type", "text/javascript");
		w.WritePropertyName ("exec");
		w.WriteStartArray ();
		w.WriteStringValue ("// Made by Courier from its Tests and Captures tabs (they are what it runs).");
		foreach (var t in r.Tests.Where (k => k.On && k.Key.Length > 0))
			w.WriteStringValue (JsTest (t));
		foreach (var c in r.Captures.Where (k => k.On && k.Key.Length > 0))
			w.WriteStringValue ("pm.environment.set(" + JsQuote (c.Key) + ", " + JsExpr (c.Value) + ");");
		w.WriteEndArray ();
		w.WriteEndObject ();
		w.WriteEndObject ();
	}

	static void WriteEvents (Utf8JsonWriter w, string kept, Request r)
	{
		bool own = r != null && (r.Tests.Any (k => k.On && k.Key.Length > 0) || r.Captures.Any (k => k.On && k.Key.Length > 0));
		if (kept.Length == 0 && !own)
			return;
		w.WritePropertyName ("event");
		w.WriteStartArray ();
		if (kept.Length > 0)
		{
			using var d = JsonDocument.Parse (kept, Json.Tolerant);
			foreach (var e in d.RootElement.Items ())
				e.WriteTo (w);
		}
		if (own)
			WriteTestEvent (w, r);
		w.WriteEndArray ();
	}

	static void WriteBody (Utf8JsonWriter w, Request r)
	{
		if (r.BodyMode == BodyMode.None)
			return;
		w.WritePropertyName ("body");
		w.WriteStartObject ();
		switch (r.BodyMode)
		{
		case BodyMode.Raw:
			w.WriteString ("mode", "raw");
			w.WriteString ("raw", r.Raw);
			w.WritePropertyName ("options");
			w.WriteStartObject ();
			w.WritePropertyName ("raw");
			w.WriteStartObject ();
			w.WriteString ("language", Request.RawNames[(int) r.RawLang]);
			w.WriteEndObject ();
			w.WriteEndObject ();
			break;
		case BodyMode.UrlEncoded:
			w.WriteString ("mode", "urlencoded");
			WriteKvs (w, "urlencoded", r.UrlEnc, false, true);
			break;
		case BodyMode.Form:
			w.WriteString ("mode", "formdata");
			WriteKvs (w, "formdata", r.Form, false, true);
			break;
		case BodyMode.Binary:
			w.WriteString ("mode", "file");
			w.WritePropertyName ("file");
			w.WriteStartObject ();
			w.WriteString ("src", r.Binary);
			w.WriteEndObject ();
			break;
		}
		w.WriteEndObject ();
	}

	static void WriteRequestItem (Utf8JsonWriter w, Request r, string id)
	{
		w.WriteString ("name", r.Name);
		if (id.Length > 0)
			w.WriteString ("id", id);
		WriteEvents (w, r.Events, r);
		if (!r.FollowRedirects)
		{
			w.WritePropertyName ("protocolProfileBehavior");
			w.WriteStartObject ();
			w.WriteBoolean ("followRedirects", false);
			w.WriteEndObject ();
		}
		w.WritePropertyName ("request");
		w.WriteStartObject ();
		WriteAuth (w, r.Auth);
		w.WriteString ("method", r.Method);
		WriteKvs (w, "header", r.Headers);
		WriteBody (w, r);
		WriteUrl (w, r);
		WriteDesc (w, r.Desc);
		w.WriteEndObject ();
		w.WritePropertyName ("response");
		w.WriteStartArray ();
		w.WriteEndArray ();
		if (r.Tests.Any (k => !k.Blank) || r.Captures.Any (k => !k.Blank) || r.TimeoutMs != 0 || r.Insecure)
		{
			w.WritePropertyName ("onyx");
			w.WriteStartObject ();
			if (r.TimeoutMs != 0)
				w.WriteNumber ("timeout", r.TimeoutMs);
			if (r.Insecure)
				w.WriteBoolean ("insecure", true);
			WriteKvs (w, "tests", r.Tests);
			WriteKvs (w, "captures", r.Captures);
			w.WriteEndObject ();
		}
	}

	public static void WriteRequestAlone (Utf8JsonWriter w, Request r)
	{
		w.WriteStartObject ();
		WriteRequestItem (w, r, "");
		w.WriteEndObject ();
	}

	static void WriteItems (Utf8JsonWriter w, Item parent)
	{
		w.WritePropertyName ("item");
		w.WriteStartArray ();
		foreach (var it in parent.Kids)
		{
			w.WriteStartObject ();
			if (it.Folder)
			{
				w.WriteString ("name", it.Name);
				w.WriteString ("id", it.Id);
				WriteDesc (w, it.Desc);
				WriteAuth (w, it.Auth);
				WriteEvents (w, it.Events, null);
				WriteItems (w, it);
			}
			else
				WriteRequestItem (w, it.Req, it.Id);
			w.WriteEndObject ();
		}
		w.WriteEndArray ();
	}

	public static void WriteCollection (Utf8JsonWriter w, Collection c)
	{
		w.WriteStartObject ();
		w.WritePropertyName ("info");
		w.WriteStartObject ();
		w.WriteString ("_postman_id", c.Id);
		w.WriteString ("name", c.Name);
		WriteDesc (w, c.Desc);
		w.WriteString ("schema", Schema21);
		w.WriteEndObject ();
		WriteItems (w, c.Root);
		if (c.Auth.Type != AuthType.None)
			WriteAuth (w, c.Auth);
		WriteEvents (w, c.Events, null);
		if (c.Vars.Any (k => !k.Blank))
			WriteKvs (w, "variable", c.Vars);
		w.WriteEndObject ();
	}

	public static void WriteEnvironment (Utf8JsonWriter w, Env e, string scope)
	{
		w.WriteStartObject ();
		w.WriteString ("id", e.Id);
		w.WriteString ("name", e.Name);
		WriteKvs (w, "values", e.Vars, true);
		w.WriteString ("_postman_variable_scope", scope);
		w.WriteEndObject ();
	}
}

/// <summary>Tout ce que Courier garde : collections, environnements, globales, historique.</summary>
class Store
{
	public const string DirRoot = "/home/courier";
	public const string DirColl = DirRoot + "/collections";
	public const string DirEnv = DirRoot + "/environments";
	public const string FileGlobals = DirRoot + "/globals.json";
	public const string FileHistory = DirRoot + "/history.json";
	public const string FileState = DirRoot + "/state.json";
	public const string FileCookies = DirRoot + "/cookies.json";
	public const int HistoryMax = 100;

	public List<Collection> Colls = new ();
	public List<Env> Envs = new ();
	public Env Globals = new () { Name = "Globals" };
	public List<HistoryEntry> History = new ();	// la plus recente d'abord
	public int ActiveEnv = -1;
	public bool HistoryDirty;

	public Env Active => ActiveEnv >= 0 && ActiveEnv < Envs.Count ? Envs[ActiveEnv] : null;

	public Collection CollectionOf (Item it)
	{
		while (it?.Parent != null)
			it = it.Parent;
		return Colls.FirstOrDefault (c => c.Root == it);
	}

	static Item Find (Item at, string id)
	{
		if (at.Id == id)
			return at;
		foreach (var k in at.Kids)
		{
			var r = Find (k, id);
			if (r != null)
				return r;
		}
		return null;
	}

	public Item FindItem (string id, out Collection coll)
	{
		coll = null;
		if (string.IsNullOrEmpty (id))
			return null;
		foreach (var c in Colls)
		{
			var r = Find (c.Root, id);
			if (r != null)
			{
				coll = c;
				return r;
			}
		}
		return null;
	}

	public static void Dirs ()
	{
		Directory.CreateDirectory (DirColl);
		Directory.CreateDirectory (DirEnv);
	}

	public static bool Write (string path, Action<Utf8JsonWriter> f, bool indented = true)
	{
		try
		{
			File.WriteAllBytes (path, Json.Make (f, indented));
			return true;
		}
		catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
		{
			return false;
		}
	}

	public bool Save (Collection c)
	{
		Dirs ();
		if (c.File.Length == 0)
			c.File = DirColl + "/" + c.Id + ".postman_collection.json";
		bool ok = Write (c.File, w => Postman.WriteCollection (w, c));
		c.Dirty = !ok;
		return ok;
	}

	public bool Save (Env e)
	{
		Dirs ();
		if (e == Globals)
			e.File = FileGlobals;
		else if (e.File.Length == 0)
			e.File = DirEnv + "/" + e.Id + ".postman_environment.json";
		bool ok = Write (e.File, w => Postman.WriteEnvironment (w, e, e == Globals ? "globals" : "environment"));
		e.Dirty = !ok;
		return ok;
	}

	public bool SaveHistory ()
	{
		Dirs ();
		bool ok = Write (FileHistory, w =>
		{
			w.WriteStartArray ();
			foreach (var h in History)
			{
				w.WriteStartObject ();
				w.WriteNumber ("when", h.When);
				w.WriteNumber ("status", h.Status);
				w.WriteNumber ("ms", h.Ms);
				w.WritePropertyName ("item");
				Postman.WriteRequestAlone (w, h.Req);
				w.WriteEndObject ();
			}
			w.WriteEndArray ();
		}, false);
		HistoryDirty = !ok;
		return ok;
	}

	public static JsonDocument Parse (string path)
	{
		try { return JsonDocument.Parse (File.ReadAllBytes (path), Json.Tolerant); }
		catch (Exception e) when (e is IOException || e is JsonException || e is UnauthorizedAccessException) { return null; }
	}

	public void Load ()
	{
		Dirs ();
		foreach (var f in Directory.GetFiles (DirColl, "*.json"))
		{
			using var d = Parse (f);
			if (d == null || !Postman.IsCollection (d.RootElement))
				continue;
			var c = Postman.ReadCollection (d.RootElement);
			c.File = f;
			c.Root.Open = false;
			Colls.Add (c);
		}
		Colls.Sort ((a, b) => string.CompareOrdinal (a.Name, b.Name));
		foreach (var f in Directory.GetFiles (DirEnv, "*.json"))
		{
			using var d = Parse (f);
			if (d == null || d.RootElement.Get ("values").ValueKind != JsonValueKind.Array)
				continue;
			var e = Postman.ReadEnvironment (d.RootElement);
			e.File = f;
			Envs.Add (e);
		}
		Envs.Sort ((a, b) => string.CompareOrdinal (a.Name, b.Name));
		using (var d = Parse (FileGlobals))
			if (d != null)
			{
				Globals = Postman.ReadEnvironment (d.RootElement);
				Globals.Name = "Globals";
			}
		if (Globals.Id.Length == 0)
			Globals.Id = Util.Uuid ();
		using (var d = Parse (FileHistory))
			if (d != null)
				foreach (var e in d.RootElement.Items ().Take (HistoryMax))
					History.Add (new HistoryEntry { When = e.Get ("when").Long (), Status = e.Get ("status").Int (), Ms = e.Get ("ms").Int (), Req = Postman.ReadRequestAlone (e.Get ("item")) });
	}
}
