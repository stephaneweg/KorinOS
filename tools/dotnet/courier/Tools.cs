// Tools.cs -- ce que Courier calcule d'une reponse et d'une requete : les expressions des Tests et Captures
// (status, time, size, body, header.<Nom>, cookie.<nom>, json, json.a.b[0].c ou $.a.b[0].c) et leurs attentes
// (« 200 », « = x », « != x », « < 500 », « >= 1 », « contains x », « matches x », « exists »), le code d'une
// requete (cURL, HTTP, Python, JavaScript), une commande cURL relue, le JSON et le XML indentes.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;

class TestResult
{
	public string Name = "", Detail = "";
	public bool Pass, Capture;
}

/// <summary>Une reponse vue par les expressions ; son JSON n'est lu qu'une fois.</summary>
sealed class Eval : IDisposable
{
	readonly Response _r;
	JsonDocument _doc;
	bool _parsed;

	public Eval (Response r) { _r = r; }
	public void Dispose () => _doc?.Dispose ();

	JsonDocument Doc ()
	{
		if (!_parsed)
		{
			_parsed = true;
			try { _doc = JsonDocument.Parse (_r.Body, Json.Tolerant); }
			catch (JsonException) { }
		}
		return _doc;
	}

	public static string Show (JsonElement v) => v.ValueKind switch
	{
		JsonValueKind.String => v.GetString (),
		JsonValueKind.Null => "null",
		JsonValueKind.True => "true",
		JsonValueKind.False => "false",
		_ => v.GetRawText (),
	};

	/// <summary>La valeur a un chemin (« .a.b[0] », « a.b », « [2].name », [-1] : la derniere).</summary>
	static bool Path (JsonElement v, string p, out JsonElement o)
	{
		o = v;
		for (int i = 0; i < p.Length;)
		{
			if (p[i] == '.')
			{
				i++;
				continue;
			}
			if (p[i] == '[')
			{
				i++;
				if (i < p.Length && (p[i] == '"' || p[i] == '\''))
				{
					int e = p.IndexOf (p[i], i + 1);
					if (e < 0)
						e = p.Length;
					if (o.ValueKind != JsonValueKind.Object || !o.TryGetProperty (p.Substring (i + 1, e - i - 1), out o))
						return false;
					i = p.IndexOf (']', Math.Min (e, p.Length));
					i = i < 0 ? p.Length : i + 1;
					continue;
				}
				int end = p.IndexOf (']', i);
				if (end < 0)
					end = p.Length;
				if (!int.TryParse (p.AsSpan (i, end - i), out int k) || o.ValueKind != JsonValueKind.Array)
					return false;
				if (k < 0)
					k += o.GetArrayLength ();
				if (k < 0 || k >= o.GetArrayLength ())
					return false;
				o = o[k];
				i = Math.Min (end + 1, p.Length);
				continue;
			}
			int stop = i;
			while (stop < p.Length && p[stop] != '.' && p[stop] != '[')
				stop++;
			string name = p.Substring (i, stop - i);
			if (o.ValueKind == JsonValueKind.Array && char.IsAsciiDigit (name[0]))
			{
				if (!int.TryParse (name, out int k) || k >= o.GetArrayLength ())
					return false;
				o = o[k];
			}
			else if (o.ValueKind != JsonValueKind.Object || !o.TryGetProperty (name, out o))
				return false;
			i = stop;
		}
		return true;
	}

	/// <summary>La valeur de l'expression ; null : elle n'en a pas (champ absent, en-tete non envoye).</summary>
	public string Get (string expr)
	{
		string t = expr.Trim ();
		if (Util.EqI (t, "status") || Util.EqI (t, "code"))
			return _r.Status.ToString ();
		if (Util.EqI (t, "time"))
			return _r.MsTotal.ToString ();
		if (Util.EqI (t, "size"))
			return _r.Body.Length.ToString ();
		if (Util.EqI (t, "body"))
			return _r.Text_;
		if (Util.EqI (t, "reason"))
			return _r.Reason;
		if (Util.StartsI (t, "header.") || Util.StartsI (t, "headers."))
			return _r.Header (t.Substring (t.IndexOf ('.') + 1));
		if (Util.StartsI (t, "cookie."))
			return _r.SetCookies.FirstOrDefault (c => c.Name == t.Substring (7))?.Value;
		if (Util.StartsI (t, "json") || t.StartsWith ('$'))
		{
			var d = Doc ();
			if (d == null || !Path (d.RootElement, t.Substring (t[0] == '$' ? 1 : 4), out var v))
				return null;
			return Show (v);
		}
		return t;					// (une constante)
	}

	static bool Number (string s, out double v) => double.TryParse (s.Trim (), NumberStyles.Float, CultureInfo.InvariantCulture, out v);

	/// <summary>Un test : le controle contre l'attendu ; detail, ce qui a ete trouve.</summary>
	public bool Test (string check, string expect, out string detail)
	{
		string got = Get (check), exp = expect.Trim (), x = exp, op = "=";
		string[] ops = { "!=", "<=", ">=", "==", "<", ">", "=" };
		var found = ops.FirstOrDefault (x.StartsWith);
		if (found != null)
		{
			op = found == "==" ? "=" : found;
			x = x.Substring (found.Length);
		}
		else if (Util.StartsI (x, "contains "))
		{
			op = "contains";
			x = x.Substring (9);
		}
		else if (Util.StartsI (x, "matches "))
		{
			op = "contains";
			x = x.Substring (8);
		}
		else if (Util.StartsI (x, "not exists") || Util.StartsI (x, "missing"))
		{
			detail = got != null ? "found" : "missing";
			return got == null;
		}
		string want = x.TrimStart ();
		if (want.Length >= 2 && want[0] == '"' && want[^1] == '"')
			want = want.Substring (1, want.Length - 2);
		if (got == null)
		{
			detail = "missing";
			return false;
		}
		detail = "got " + (got.Length > 80 ? got.Substring (0, 80) + "..." : got);
		if (exp.Length == 0 || Util.EqI (want, "exists"))
			return true;
		bool num = Number (got, out double a) & Number (want, out double b);
		switch (op)
		{
		case "contains": return Util.HasI (got, want);
		case "=": return num ? a == b : got == want;
		case "!=": return num ? a != b : got != want;
		}
		if (!num)
		{
			detail += " (not a number)";
			return false;
		}
		return op switch { "<" => a < b, "<=" => a <= b, ">" => a > b, _ => a >= b };
	}
}

static class Snippet
{
	public static readonly string[] Names = { "cURL", "HTTP", "Python - requests", "JavaScript - fetch" };

	static string Sh (string s) => "'" + s.Replace ("'", "'\\''") + "'";
	static string C (string s) => "\"" + s.Replace ("\\", "\\\\").Replace ("\"", "\\\"").Replace ("\n", "\\n").Replace ("\r", "\\r").Replace ("\t", "\\t") + "\"";

	static bool Default (KV h) => (Util.EqI (h.Key, "User-Agent") && h.Value == Prepared.UserAgent) || (Util.EqI (h.Key, "Accept") && h.Value == "*/*")
		|| (Util.EqI (h.Key, "Accept-Encoding") && h.Value == "gzip, deflate");

	/// <summary>Le code de la requete, ses variables resolues, dans le langage choisi.</summary>
	public static string Make (int lang, Request r, Scope sc, Auth auth)
	{
		var p = Prepared.Make (r, sc, auth, new CookieJar ());
		bool form = r.BodyMode == BodyMode.Form, bin = r.BodyMode == BodyMode.Binary;
		var hs = p.Headers.Where (h => !Default (h) && !(form && Util.EqI (h.Key, "Content-Type") && Util.StartsI (h.Value, "multipart/"))).ToList ();
		var fields = !form ? new List<KV> () : r.Form.Where (f => f.On && f.Key.Length > 0).Select (f => new KV (sc.Resolve (f.Key), sc.Resolve (f.Value)) { File = f.File }).ToList ();
		string binPath = bin ? sc.Resolve (r.Binary) : "", body = Encoding.UTF8.GetString (p.Body);
		var o = new StringBuilder ();
		switch (lang)
		{
		case 0:
			o.Append ("curl --location");
			if (p.Method != "GET")
				o.Append (" --request ").Append (p.Method);
			o.Append (' ').Append (Sh (p.Url));
			foreach (var h in hs)
				o.Append (" \\\n  --header ").Append (Sh (h.Key + ": " + h.Value));
			if (form)
				foreach (var f in fields)
					o.Append (" \\\n  --form ").Append (Sh (f.Key + (f.File ? "=@\"" : "=\"") + f.Value + "\""));
			else if (bin)
				o.Append (" \\\n  --data-binary ").Append (Sh ("@" + binPath));
			else if (p.Body.Length > 0)
				o.Append (" \\\n  --data-raw ").Append (Sh (body));
			o.Append ('\n');
			break;
		case 1:
			Uri.TryCreate (p.Url, UriKind.Absolute, out var u);
			o.Append (p.Method).Append (' ').Append (u?.PathAndQuery ?? "/").Append (" HTTP/1.1\nHost: ").Append (u?.Authority).Append ('\n');
			foreach (var h in hs)
				o.Append (h.Key).Append (": ").Append (h.Value).Append ('\n');
			if (form)
			{
				o.Append ("Content-Type: multipart/form-data; boundary=----CourierBoundary\n\n");	// (refait, lisible)
				foreach (var f in fields)
				{
					o.Append ("------CourierBoundary\n");
					if (f.File)
						o.Append ("Content-Disposition: form-data; name=\"" + f.Key + "\"; filename=\"" + Util.BaseName (f.Value) + "\"\nContent-Type: " + Util.Mime (f.Value) + "\n\n(the file's bytes: " + f.Value + ")\n");
					else
						o.Append ("Content-Disposition: form-data; name=\"" + f.Key + "\"\n\n" + f.Value + "\n");
				}
				o.Append ("------CourierBoundary--\n");
			}
			else if (bin)
				o.Append ("Content-Length: " + p.Body.Length + "\n\n(the file's bytes: " + binPath + ")\n");
			else if (p.Body.Length > 0)
				o.Append ("Content-Length: " + p.Body.Length + "\n\n" + body + "\n");
			break;
		case 2:
			o.Append ("import requests\n\nurl = ").Append (C (p.Url)).Append ("\n\n");
			if (form)
			{
				o.Append ("payload = {").Append (string.Join (", ", fields.Where (f => !f.File).Select (f => C (f.Key) + ": " + C (f.Value)))).Append ("}\nfiles = [");
				var files = fields.Where (f => f.File).Select (f => "\n  (" + C (f.Key) + ", (" + C (Util.BaseName (f.Value)) + ", open(" + C (f.Value) + ", \"rb\"), " + C (Util.Mime (f.Value)) + "))").ToList ();
				o.Append (string.Join (",", files)).Append (files.Count == 0 ? "]\n" : "\n]\n");
			}
			else if (bin)
				o.Append ("payload = open(").Append (C (binPath)).Append (", \"rb\").read()\n");
			else
				o.Append ("payload = ").Append (C (body)).Append ('\n');
			o.Append ("headers = {").Append (string.Join (",", hs.Select (h => "\n  " + C (h.Key) + ": " + C (h.Value)))).Append (hs.Count > 0 ? "\n}\n\n" : "}\n\n");
			o.Append ("response = requests.request(\"" + p.Method + "\", url, headers=headers, data=payload" + (form ? ", files=files" : "") + ")\n\nprint(response.text)\n");
			break;
		case 3:
			o.Append ("const myHeaders = new Headers();\n");
			foreach (var h in hs)
				o.Append ("myHeaders.append(" + C (h.Key) + ", " + C (h.Value) + ");\n");
			o.Append ('\n');
			if (form)
			{
				o.Append ("const body = new FormData();\n");
				foreach (var f in fields)
					o.Append ("body.append(" + C (f.Key) + ", " + (f.File ? "fileInput.files[0], " + C (Util.BaseName (f.Value)) : C (f.Value)) + ");\n");
				o.Append ('\n');
			}
			else if (bin)
				o.Append ("const body = fileInput.files[0];\n\n");
			else if (p.Body.Length > 0)
				o.Append ("const body = " + C (body) + ";\n\n");
			o.Append ("const requestOptions = {\n  method: " + C (p.Method) + ",\n  headers: myHeaders,\n");
			if (form || bin || p.Body.Length > 0)
				o.Append ("  body: body,\n");
			o.Append ("  redirect: \"follow\"\n};\n\nfetch(" + C (p.Url) + ", requestOptions)\n  .then((response) => response.text())\n");
			o.Append ("  .then((result) => console.log(result))\n  .catch((error) => console.error(error));\n");
			break;
		}
		return o.ToString ();
	}
}

static class Curl
{
	/// <summary>Les mots d'une ligne de commande : guillemets, $'...', fins de ligne echappees.</summary>
	static List<string> Words (string s)
	{
		var o = new List<string> ();
		var w = new StringBuilder ();
		bool inside = false;
		for (int i = 0; i < s.Length;)
		{
			char c = s[i];
			char next = i + 1 < s.Length ? s[i + 1] : '\0';
			if ((c == '\\' || c == '^') && (next == '\n' || (next == '\r' && i + 2 < s.Length && s[i + 2] == '\n')))
			{
				i += next == '\n' ? 2 : 3;
				continue;
			}
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			{
				if (inside)
				{
					o.Add (w.ToString ());
					w.Clear ();
					inside = false;
				}
				i++;
				continue;
			}
			inside = true;
			if (c == '\'')
			{
				for (i++; i < s.Length && s[i] != '\''; i++)
					w.Append (s[i]);
				i++;
			}
			else if (c == '"')
			{
				for (i++; i < s.Length && s[i] != '"'; i++)
				{
					if (s[i] == '\\' && i + 1 < s.Length && "\"\\$`".Contains (s[i + 1]))
						i++;
					w.Append (s[i]);
				}
				i++;
			}
			else if (c == '$' && next == '\'')
			{
				for (i += 2; i < s.Length && s[i] != '\''; i++)
				{
					if (s[i] == '\\' && i + 1 < s.Length)
					{
						i++;
						w.Append (s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i] == 'r' ? '\r' : s[i]);
					}
					else
						w.Append (s[i]);
				}
				i++;
			}
			else if (c == '\\' && next != '\0')
			{
				w.Append (next);
				i += 2;
			}
			else
			{
				w.Append (c);
				i++;
			}
		}
		if (inside)
			o.Add (w.ToString ());
		return o;
	}

	/// <summary>Une commande cURL en requete ; null : ce n'en est pas une (err dit pourquoi).</summary>
	public static Request Parse (string cmd, out string err)
	{
		err = "";
		var a = Words (cmd);
		if (a.Count == 0 || !(a[0] == "curl" || a[0].EndsWith ("/curl") || Util.EqI (a[0], "curl.exe")))
		{
			err = "It does not start with curl.";
			return null;
		}
		var r = new Request ();
		string method = "";
		var data = new StringBuilder ();
		bool get = false, dataSeen = false;
		var form = new List<KV> ();
		for (int i = 1; i < a.Count; i++)
		{
			string w = a[i], next = i + 1 < a.Count ? a[i + 1] : null;
			bool Arg (params string[] names) => names.Contains (w);
			if (Arg ("-X", "--request"))
			{
				method = next ?? "";
				i++;
			}
			else if (w.StartsWith ("-X") && w.Length > 2)
				method = w.Substring (2);
			else if (Arg ("-H", "--header"))
			{
				if (next == null)
					break;
				int c = next.IndexOf (':');
				if (c >= 0)
					r.Headers.Add (new KV (next.Substring (0, c).Trim (), next.Substring (c + 1).Trim ()));
				i++;
			}
			else if (Arg ("-d", "--data", "--data-raw", "--data-binary", "--data-ascii", "--data-urlencode"))
			{
				if (next == null)
					break;
				if (dataSeen)
					data.Append ('&');
				if (w == "--data-urlencode")
				{
					int eq = next.IndexOf ('=');
					data.Append (eq >= 0 ? next.Substring (0, eq + 1) + Util.UrlEncode (next.Substring (eq + 1), true) : Util.UrlEncode (next, true));
				}
				else
					data.Append (next);
				dataSeen = true;
				i++;
			}
			else if (Arg ("-F", "--form", "--form-string"))
			{
				if (next == null)
					break;
				int eq = next.IndexOf ('=');
				if (eq >= 0)
				{
					var kv = new KV (next.Substring (0, eq), next.Substring (eq + 1));
					if (kv.Value.StartsWith ('@') && w != "--form-string")
					{
						kv.File = true;
						kv.Value = kv.Value.Substring (1);
						int semi = kv.Value.IndexOf (';');
						if (semi >= 0)
							kv.Value = kv.Value.Substring (0, semi);
					}
					if (kv.Value.Length >= 2 && kv.Value[0] == '"' && kv.Value[^1] == '"')
						kv.Value = kv.Value.Substring (1, kv.Value.Length - 2);
					form.Add (kv);
				}
				i++;
			}
			else if (Arg ("-u", "--user"))
			{
				if (next == null)
					break;
				int c = next.IndexOf (':');
				r.Auth.Type = AuthType.Basic;
				r.Auth.User = c >= 0 ? next.Substring (0, c) : next;
				r.Auth.Pass = c >= 0 ? next.Substring (c + 1) : "";
				i++;
			}
			else if (Arg ("-A", "--user-agent") || Arg ("-b", "--cookie") || Arg ("-e", "--referer"))
			{
				if (next != null)
					r.Headers.Add (new KV (Arg ("-A", "--user-agent") ? "User-Agent" : Arg ("-b", "--cookie") ? "Cookie" : "Referer", next));
				i++;
			}
			else if (Arg ("--url"))
			{
				r.Url = next ?? "";
				i++;
			}
			else if (Arg ("-G", "--get"))
				get = true;
			else if (Arg ("-I", "--head"))
				method = "HEAD";
			else if (Arg ("-o", "--output", "-m", "--max-time", "--connect-timeout", "-x", "--proxy", "-w", "--write-out", "--retry", "-c", "--cookie-jar", "-E", "--cert"))
				i++;
			else if (w.StartsWith ('-')) { }		// (-L, -k, -s, --compressed : rien a garder)
			else if (r.Url.Length == 0)
				r.Url = w;
		}
		if (r.Url.Length == 0)
		{
			err = "The command has no URL.";
			return null;
		}
		if (get && dataSeen)
		{
			r.Url += (r.Url.Contains ('?') ? '&' : '?') + data.ToString ();
			dataSeen = false;
		}
		if (form.Count > 0)
		{
			r.BodyMode = BodyMode.Form;
			r.Form = form;
			if (method.Length == 0)
				method = "POST";
		}
		else if (dataSeen)
		{
			if (method.Length == 0)
				method = "POST";
			string ct = r.Headers.LastOrDefault (h => Util.EqI (h.Key, "Content-Type"))?.Value;
			if (ct == null || Util.StartsI (ct, "application/x-www-form-urlencoded"))
			{
				r.BodyMode = BodyMode.UrlEncoded;		// (les champs decodes, comme Postman les montre)
				foreach (var part in data.ToString ().Split ('&'))
				{
					int eq = part.IndexOf ('=');
					r.UrlEnc.Add (eq < 0 ? new KV (Util.UrlDecode (part), "") : new KV (Util.UrlDecode (part.Substring (0, eq)), Util.UrlDecode (part.Substring (eq + 1))));
				}
				r.Headers.RemoveAll (h => Util.EqI (h.Key, "Content-Type"));
			}
			else
			{
				r.BodyMode = BodyMode.Raw;
				r.Raw = data.ToString ();
				r.RawLang = Util.HasI (ct, "json") ? RawLang.Json : Util.HasI (ct, "xml") ? RawLang.Xml : Util.HasI (ct, "html") ? RawLang.Html : RawLang.Text;
				r.Headers.RemoveAll (h => Util.EqI (h.Key, "Content-Type") && h.Value == Request.RawTypes[(int) r.RawLang]);
			}
		}
		r.Method = method.Length == 0 ? "GET" : method;
		r.Params = Postman.ParamsOfUrl (r.Url, r.Params);
		r.Name = Uri.TryCreate (r.Url.Contains ("://") ? r.Url : "http://" + r.Url, UriKind.Absolute, out var u) ? u.Host + (u.AbsolutePath == "/" ? "" : u.AbsolutePath) : r.Url;
		return r;
	}
}

static class Pretty
{
	static void Line (StringBuilder o, int depth)
	{
		o.Append ('\n');
		o.Append (' ', depth * 2);
	}

	/// <summary>Le JSON reindente, ses jetons tels quels (texte des nombres, ordre des cles) ; null : pas du JSON.</summary>
	public static string Json (string s)
	{
		int i = 0, n = s.Length, depth = 0;
		while (i < n && char.IsWhiteSpace (s[i]))
			i++;
		if (i >= n || (s[i] != '{' && s[i] != '['))
			return null;
		var o = new StringBuilder (n + n / 4);
		for (; i < n; i++)
		{
			char c = s[i];
			if (c == '"')
			{
				int e = i + 1;
				while (e < n && s[e] != '"')
					e += s[e] == '\\' ? 2 : 1;
				if (e >= n)
					return null;
				o.Append (s, i, e - i + 1);
				i = e;
			}
			else if (c == '{' || c == '[')
			{
				int j = i + 1;
				while (j < n && char.IsWhiteSpace (s[j]))
					j++;
				if (j < n && ((c == '{' && s[j] == '}') || (c == '[' && s[j] == ']')))	// (vide : {} / [])
				{
					o.Append (c).Append (s[j]);
					i = j;
					continue;
				}
				o.Append (c);
				Line (o, ++depth);
			}
			else if (c == '}' || c == ']')
			{
				if (--depth < 0)
					return null;
				Line (o, depth);
				o.Append (c);
			}
			else if (c == ',')
			{
				o.Append (',');
				Line (o, depth);
			}
			else if (c == ':')
				o.Append (": ");
			else if (!char.IsWhiteSpace (c))
				o.Append (c);
		}
		return depth != 0 ? null : o.Append ('\n').ToString ();
	}

	/// <summary>Le XML reindente, une balise par ligne ; &lt;a&gt;texte&lt;/a&gt; reste sur la sienne. null : pas du XML.</summary>
	public static string Xml (string s)
	{
		int i = 0, n = s.Length, depth = 0;
		while (i < n && char.IsWhiteSpace (s[i]))
			i++;
		if (i >= n || s[i] != '<')
			return null;
		var o = new StringBuilder ();
		bool start = true;
		while (i < n)
		{
			if (s[i] != '<')
			{
				int e = s.IndexOf ('<', i);
				if (e < 0)
					e = n;
				string t = s.Substring (i, e - i).Trim ();
				if (t.Length > 0)
				{
					Line (o, depth);
					o.Append (t);
				}
				i = e;
				continue;
			}
			int end = i + 1;
			if (string.CompareOrdinal (s, i, "<!--", 0, 4) == 0)
			{
				end = s.IndexOf ("-->", i, StringComparison.Ordinal);
				end = end < 0 ? n : end + 3;
			}
			else
			{
				char q = '\0';
				while (end < n && (q != '\0' || s[end] != '>'))
				{
					if (q == '\0' && (s[end] == '"' || s[end] == '\''))
						q = s[end];
					else if (q == s[end])
						q = '\0';
					end++;
				}
				end = Math.Min (end + 1, n);
			}
			bool closing = i + 1 < n && s[i + 1] == '/', self = end >= 2 && s[end - 2] == '/', decl = i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!');
			if (closing)
				depth = Math.Max (0, depth - 1);
			if (!start)
				o.Append ('\n');
			o.Append (' ', depth * 2).Append (s, i, end - i);
			start = false;
			i = end;
			if (!closing && !self && !decl)
			{
				depth++;
				int t = s.IndexOf ('<', end);
				if (t >= 0 && t + 1 < n && s[t + 1] == '/')
				{
					int ce = s.IndexOf ('>', t);
					ce = ce < 0 ? n : ce + 1;
					o.Append (s.Substring (end, t - end).Trim ()).Append (s, t, ce - t);
					depth--;
					i = ce;
				}
			}
		}
		return o.Append ('\n').ToString ();
	}

	static readonly string[] Blocks = { "p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6", "title", "section", "article", "header", "footer", "table", "ul", "ol", "pre", "hr" };
	static readonly (string e, string t)[] Entities = { ("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"), ("&quot;", "\""), ("&#39;", "'"), ("&apos;", "'"), ("&nbsp;", " "), ("&copy;", "©"), ("&eacute;", "é"), ("&egrave;", "è") };

	/// <summary>Une page HTML en texte (l'apercu) : sans balises, les blocs en lignes, les entites decodees.</summary>
	public static string HtmlText (string s)
	{
		var o = new StringBuilder ();
		bool skip = false;
		int blank = 0, n = s.Length;
		for (int i = 0; i < n;)
		{
			if (s[i] == '<')
			{
				int e = s.IndexOf ('>', i);
				if (e < 0)
					e = n - 1;
				int a = i + 1 < n && s[i + 1] == '/' ? i + 2 : i + 1, b = a;
				while (b < e && char.IsAsciiLetterOrDigit (s[b]))
					b++;
				string tag = s.Substring (a, b - a).ToLowerInvariant ();
				bool closing = a == i + 2;
				if (tag == "script" || tag == "style" || tag == "head")
					skip = !closing;
				if (Array.IndexOf (Blocks, tag) >= 0)
				{
					if (o.Length > 0 && o[^1] != '\n')
						o.Append ('\n');
					else if (blank < 1 && o.Length > 0)
					{
						o.Append ('\n');
						blank++;
					}
				}
				if (tag == "li" && !closing)
					o.Append ("  - ");
				i = e + 1;
				continue;
			}
			if (skip)
			{
				i++;
				continue;
			}
			if (s[i] == '&')
			{
				var hit = Entities.FirstOrDefault (x => string.Compare (s, i, x.e, 0, x.e.Length, StringComparison.OrdinalIgnoreCase) == 0);
				if (hit.e != null)
				{
					o.Append (hit.t);
					i += hit.e.Length;
					continue;
				}
			}
			if (char.IsWhiteSpace (s[i]))
			{
				if (o.Length > 0 && !char.IsWhiteSpace (o[^1]))
					o.Append (' ');
				i++;
				continue;
			}
			o.Append (s[i++]);
			blank = 0;
		}
		return o.ToString ();
	}
}
