// Net.cs -- le moteur HTTP de Courier sur HttpClient : les {{variables}}, une requete preparee (auth, corps
// brut, urlencoded, multipart avec fichiers, fichier binaire, cookies du bocal), envoyee sans bloquer la
// fenetre et annulable ; redirections suivies ici pour garder leurs cookies et leur trace, gzip et deflate
// decodes ici pour compter ce qui a circule. TLS : OpenSSL par SslStream, verifie par /etc/ca-bundle.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

/// <summary>Chaines, nombres, dates : ce que les trois moities de Courier partagent.</summary>
static class Util
{
	public static bool EqI (string a, string b) => string.Equals (a, b, StringComparison.OrdinalIgnoreCase);
	public static bool StartsI (string a, string b) => a.StartsWith (b, StringComparison.OrdinalIgnoreCase);
	public static bool HasI (string a, string b) => a.Contains (b, StringComparison.OrdinalIgnoreCase);
	public static string Uuid () => Guid.NewGuid ().ToString ();
	public static long Now => DateTimeOffset.UtcNow.ToUnixTimeSeconds ();
	public static string Iso (long t) => DateTimeOffset.FromUnixTimeSeconds (t).ToString ("yyyy-MM-dd'T'HH:mm:ss'.000Z'");

	/// <summary>RFC 3986 ; dans un formulaire, l'espace devient +.</summary>
	public static string UrlEncode (string s, bool spacePlus = false)
	{
		var o = new StringBuilder ();
		foreach (byte c in Encoding.UTF8.GetBytes (s))
		{
			if (char.IsAsciiLetterOrDigit ((char) c) || c == '-' || c == '_' || c == '.' || c == '~')
				o.Append ((char) c);
			else if (c == ' ' && spacePlus)
				o.Append ('+');
			else
				o.Append ('%').Append (c.ToString ("X2"));
		}
		return o.ToString ();
	}

	/// <summary>Le chemin et la requete d'une URL : seul ce qui la casserait, comme Postman.</summary>
	public static string UrlSoft (string s)
	{
		var o = new StringBuilder ();
		foreach (byte c in Encoding.UTF8.GetBytes (s))
		{
			if (c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>')
				o.Append ('%').Append (c.ToString ("X2"));
			else
				o.Append ((char) c);
		}
		return o.ToString ();
	}

	public static string UrlDecode (string s) => Uri.UnescapeDataString (s.Replace ('+', ' '));

	public static string Size (long n)
	{
		if (n < 1024)
			return n + " B";
		if (n < 1024 * 1024)
			return n / 1024 + "." + (n % 1024) * 10 / 1024 + " KB";
		return n / (1024 * 1024) + "." + ((n % (1024 * 1024)) * 100 / (1024 * 1024)).ToString ("00") + " MB";
	}

	public static string SafeName (string s)
	{
		var o = new StringBuilder ();
		foreach (char c in s)
		{
			if (char.IsAsciiLetterOrDigit (c) || c == '-' || c == '_')
				o.Append (c);
			else if (c == ' ' || c == '.')
				o.Append ('_');
		}
		return o.Length == 0 ? "untitled" : o.ToString ();
	}

	public static string BaseName (string p) => p.Substring (p.LastIndexOf ('/') + 1);

	public static string Mime (string path)
	{
		int dot = path.LastIndexOf ('.');
		return (dot < 0 ? "" : path.Substring (dot + 1).ToLowerInvariant ()) switch
		{
			"json" => "application/json", "txt" => "text/plain", "png" => "image/png", "jpg" or "jpeg" => "image/jpeg", "gif" => "image/gif",
			"xml" => "application/xml", "html" or "htm" => "text/html", "csv" => "text/csv", "pdf" => "application/pdf",
			_ => "application/octet-stream",
		};
	}
}

/// <summary>Les {{variables}} : l'environnement choisi, puis la collection, puis les globales.</summary>
class Scope
{
	public Env Env, Globals;
	public Collection Coll;

	static readonly string[] First = { "Ada", "Alan", "Grace", "Linus", "Margaret", "Dennis", "Barbara", "Ken", "Frances", "Tim" };
	static readonly string[] Last = { "Lovelace", "Turing", "Hopper", "Torvalds", "Hamilton", "Ritchie", "Liskov", "Thompson", "Allen", "Berners-Lee" };
	static readonly string[] City = { "Paris", "Lyon", "Brussels", "Geneva", "Montreal", "Lille", "Nantes", "Bordeaux", "Liege", "Quebec" };
	static readonly string[] Word = { "onyx", "courier", "zircon", "quartz", "agate", "garnet", "topaz", "beryl", "jade", "opal" };
	static readonly string[] Color = { "red", "green", "blue", "orange", "purple", "teal", "yellow", "black", "white", "grey" };
	static string Pick (string[] a) => a[Random.Shared.Next (a.Length)];

	static string Dynamic (string name) => name switch
	{
		"$guid" or "$randomUUID" => Util.Uuid (),
		"$timestamp" => Util.Now.ToString (),
		"$isoTimestamp" => Util.Iso (Util.Now),
		"$randomInt" => Random.Shared.Next (1001).ToString (),
		"$randomBoolean" => Random.Shared.Next (2) == 0 ? "true" : "false",
		"$randomAlphaNumeric" => "abcdefghijklmnopqrstuvwxyz0123456789"[Random.Shared.Next (36)].ToString (),
		"$randomFirstName" => Pick (First),
		"$randomLastName" => Pick (Last),
		"$randomCity" => Pick (City),
		"$randomWord" => Pick (Word),
		"$randomColor" => Pick (Color),
		"$randomEmail" => (Pick (First) + "." + Pick (Last) + Random.Shared.Next (100) + "@example.com").ToLowerInvariant (),
		_ => null,
	};

	static string Find (List<KV> l, string name) => l.LastOrDefault (k => k.On && k.Key == name)?.Value;

	string Lookup (string name)
	{
		if (name.StartsWith ('$'))
			return Dynamic (name);
		return (Env == null ? null : Find (Env.Vars, name)) ?? (Coll == null ? null : Find (Coll.Vars, name)) ?? (Globals == null ? null : Find (Globals.Vars, name));
	}

	/// <summary>s, ses {{variables}} remplacees ; une inconnue reste ecrite, comme dans Postman.</summary>
	public string Resolve (string s, int depth = 0)
	{
		int i = s.IndexOf ("{{", StringComparison.Ordinal);
		if (i < 0)
			return s;
		var o = new StringBuilder ();
		int from = 0;
		for (; i >= 0; i = s.IndexOf ("{{", from, StringComparison.Ordinal))
		{
			int end = s.IndexOf ("}}", i + 2, StringComparison.Ordinal);
			if (end < 0)
				break;
			string inner = s.Substring (i + 2, end - i - 2);
			int nested = inner.LastIndexOf ('{');
			if (nested >= 0 || inner.Contains ('\n'))
			{
				o.Append (s, from, i + 2 - from);
				from = i + 2;
				continue;
			}
			o.Append (s, from, i - from);
			string name = inner.Trim (), v = name.Length == 0 ? null : Lookup (name);
			if (v == null)
				o.Append (s, i, end + 2 - i);
			else
				o.Append (depth < 4 ? Resolve (v, depth + 1) : v);
			from = end + 2;
		}
		o.Append (s, from, s.Length - from);
		return o.ToString ();
	}

	/// <summary>Pose une variable (captures) : avant la ligne vide de la fin.</summary>
	public static void Set (List<KV> l, string name, string value)
	{
		var kv = l.FirstOrDefault (k => k.Key == name);
		if (kv != null)
		{
			kv.Value = value;
			kv.On = true;
			return;
		}
		int at = l.Count;
		while (at > 0 && l[at - 1].Blank)
			at--;
		l.Insert (at, new KV (name, value));
	}
}

class Cookie
{
	public string Domain = "", Path = "", Name = "", Value = "";
	public long Expires;			// secondes unix, 0 : cookie de session
	public bool Secure, HostOnly = true, HttpOnly;
}

class CookieJar
{
	public List<Cookie> List = new ();
	public bool Dirty;

	static bool DomainMatch (string host, Cookie c)
	{
		if (c.HostOnly)
			return Util.EqI (host, c.Domain);
		return Util.EqI (host, c.Domain) || host.EndsWith ("." + c.Domain, StringComparison.OrdinalIgnoreCase);
	}

	static bool PathMatch (string path, Cookie c)
	{
		if (c.Path.Length == 0 || c.Path == "/")
			return true;
		if (!path.StartsWith (c.Path, StringComparison.Ordinal))
			return false;
		return path.Length == c.Path.Length || path[c.Path.Length] == '/' || path[c.Path.Length] == '?' || c.Path.EndsWith ('/');
	}

	public string Header (Uri u)
	{
		long now = Util.Now;
		bool https = u.Scheme == "https";
		var hit = List.Where (c => (c.Expires == 0 || c.Expires >= now) && (!c.Secure || https) && DomainMatch (u.Host, c) && PathMatch (u.AbsolutePath, c));
		return string.Join ("; ", hit.Select (c => c.Name + "=" + c.Value));
	}

	/// <summary>La valeur d'un Set-Cookie recu de u.</summary>
	public Cookie Set (string v, Uri u)
	{
		var parts = v.Split (';');
		int eq = parts[0].IndexOf ('=');
		if (eq < 0)
			return null;
		var c = new Cookie { Name = parts[0].Substring (0, eq).Trim (), Value = parts[0].Substring (eq + 1).Trim (), Domain = u.Host };
		int sl = u.AbsolutePath.LastIndexOf ('/');
		c.Path = sl <= 0 ? "/" : u.AbsolutePath.Substring (0, sl);
		long maxAge = -1;
		foreach (var a in parts.Skip (1))
		{
			int q = a.IndexOf ('=');
			string k = (q < 0 ? a : a.Substring (0, q)).Trim (), val = q < 0 ? "" : a.Substring (q + 1).Trim ();
			if (Util.EqI (k, "domain") && val.Length > 0)
			{
				c.Domain = val.TrimStart ('.');
				c.HostOnly = false;
			}
			else if (Util.EqI (k, "path") && val.Length > 0)
				c.Path = val;
			else if (Util.EqI (k, "secure"))
				c.Secure = true;
			else if (Util.EqI (k, "httponly"))
				c.HttpOnly = true;
			else if (Util.EqI (k, "max-age"))
				long.TryParse (val, out maxAge);
			else if (Util.EqI (k, "expires") && DateTimeOffset.TryParse (val, out var d))
				c.Expires = d.ToUnixTimeSeconds ();
		}
		if (maxAge >= 0)
			c.Expires = maxAge == 0 ? 1 : Util.Now + maxAge;
		bool gone = c.Expires != 0 && c.Expires < Util.Now;
		List.RemoveAll (o => o.Name == c.Name && Util.EqI (o.Domain, c.Domain) && o.Path == c.Path);
		if (!gone)
			List.Add (c);
		Dirty = true;
		return c;
	}

	public CookieJar Copy () => new CookieJar { List = new List<Cookie> (List) };
}

/// <summary>Une requete prete a partir : variables resolues, en-tetes et corps faits.</summary>
class Prepared
{
	public const string UserAgent = "Courier/1.0 (ARMPI)";
	public string Method = "GET", Url = "", Error = "";
	public List<KV> Headers = new ();
	public byte[] Body = Array.Empty<byte> ();
	public bool Follow = true, Insecure;
	public int TimeoutMs = 30000;

	public void SetHeader (string k, string v)
	{
		if (!Has (k))
			Headers.Add (new KV (k, v));
	}
	public bool Has (string k) => Headers.Any (h => Util.EqI (h.Key, k));

	public static Prepared Make (Request r, Scope sc, Auth auth, CookieJar jar)
	{
		var p = new Prepared { Method = r.Method.Length == 0 ? "GET" : r.Method.ToUpperInvariant (), Follow = r.FollowRedirects, Insecure = r.Insecure };
		string url = sc.Resolve (r.Url).Trim ();
		if (url.Length > 0 && !url.Contains ("://"))
			url = "http://" + url;
		if (r.TimeoutMs > 0)
			p.TimeoutMs = r.TimeoutMs;
		foreach (var h in r.Headers.Where (x => x.On && x.Key.Length > 0))
			p.Headers.Add (new KV (sc.Resolve (h.Key).Trim (), sc.Resolve (h.Value)));
		switch (auth.Type)
		{
		case AuthType.Bearer:
			p.SetHeader ("Authorization", "Bearer " + sc.Resolve (auth.Token));
			break;
		case AuthType.Basic:
			p.SetHeader ("Authorization", "Basic " + Convert.ToBase64String (Encoding.UTF8.GetBytes (sc.Resolve (auth.User) + ":" + sc.Resolve (auth.Pass))));
			break;
		case AuthType.ApiKey:
			string k = sc.Resolve (auth.Key), v = sc.Resolve (auth.Value);
			if (k.Length > 0 && auth.InQuery)
				url += (url.Contains ('?') ? "&" : "?") + Util.UrlEncode (k) + "=" + Util.UrlEncode (v);
			else if (k.Length > 0)
				p.SetHeader (k, v);
			break;
		}
		p.Url = url;
		string ctype = null;
		switch (r.BodyMode)
		{
		case BodyMode.Raw:
			p.Body = Encoding.UTF8.GetBytes (sc.Resolve (r.Raw));
			ctype = Request.RawTypes[(int) r.RawLang];
			break;
		case BodyMode.UrlEncoded:
			var form = r.UrlEnc.Where (x => x.On && x.Key.Length > 0).Select (x => Util.UrlEncode (sc.Resolve (x.Key), true) + "=" + Util.UrlEncode (sc.Resolve (x.Value), true));
			p.Body = Encoding.ASCII.GetBytes (string.Join ("&", form));
			ctype = "application/x-www-form-urlencoded";
			break;
		case BodyMode.Form:
			string boundary = "----CourierFormBoundary" + Guid.NewGuid ().ToString ("N").Substring (0, 16);
			using (var b = new MemoryStream ())
			{
				void Add (string s) => b.Write (Encoding.UTF8.GetBytes (s));
				foreach (var kv in r.Form.Where (x => x.On && x.Key.Length > 0))
				{
					string key = sc.Resolve (kv.Key), val = sc.Resolve (kv.Value);
					Add ("--" + boundary + "\r\n");
					if (kv.File)
					{
						byte[] f;
						try { f = File.ReadAllBytes (val); }
						catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
						{
							p.Error = "The file of the form's field \"" + key + "\" cannot be read: " + val;
							return p;
						}
						Add ("Content-Disposition: form-data; name=\"" + key + "\"; filename=\"" + Util.BaseName (val) + "\"\r\nContent-Type: " + Util.Mime (val) + "\r\n\r\n");
						b.Write (f);
						Add ("\r\n");
					}
					else
						Add ("Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n" + val + "\r\n");
				}
				Add ("--" + boundary + "--\r\n");
				p.Body = b.ToArray ();
			}
			ctype = "multipart/form-data; boundary=" + boundary;
			break;
		case BodyMode.Binary:
			if (r.Binary.Length == 0)
				break;
			string path = sc.Resolve (r.Binary);
			try { p.Body = File.ReadAllBytes (path); }
			catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
			{
				p.Error = "The body's file cannot be read: " + path;
				return p;
			}
			ctype = Util.Mime (path);
			break;
		}
		if (ctype != null)
			p.SetHeader ("Content-Type", ctype);
		p.SetHeader ("User-Agent", UserAgent);
		p.SetHeader ("Accept", "*/*");
		p.SetHeader ("Accept-Encoding", "gzip, deflate");
		if (!p.Has ("Cookie") && Uri.TryCreate (url, UriKind.Absolute, out var u))
		{
			string c = jar.Header (u);
			if (c.Length > 0)
				p.Headers.Add (new KV ("Cookie", c));
		}
		return p;
	}
}

class Response
{
	public int Status;			// le statut HTTP ; < 0 : echec (Error dit pourquoi)
	public string Reason = "", Error = "", HttpVersion = "", FinalUrl = "";
	public List<KV> Headers = new ();
	public byte[] Body = Array.Empty<byte> ();
	public long WireLen, HeadLen;
	public int MsTotal, MsConnect, MsFirst, Redirects;
	public StringBuilder Console = new ();
	public List<Cookie> SetCookies = new ();
	public bool Truncated, Cancelled;
	string _text;

	public string Header (string name) => Headers.FirstOrDefault (h => Util.EqI (h.Key, name))?.Value;
	public string Text_ => _text ??= Encoding.UTF8.GetString (Body);
}

/// <summary>Un envoi : Run fait tout sur un fil du pool, Cancel l'arrete.</summary>
class Job
{
	const int RespMax = 16 << 20;		// plus gros : coupe (Truncated)
	public Prepared Prep;
	public CookieJar Jar;			// une copie de celui de l'application
	public Response Resp = new ();
	readonly CancellationTokenSource _cancel = new ();

	public void Cancel () => _cancel.Cancel ();

	sealed class Failed : Exception { public int Code; public Failed (int code, string msg) : base (msg) { Code = code; } }

	static Uri Parse (string url)
	{
		int hash = url.IndexOf ('#');
		string u = Util.UrlSoft (hash >= 0 ? url.Substring (0, hash) : url);
		if (!Uri.TryCreate (u, UriKind.Absolute, out var r) || (r.Scheme != "http" && r.Scheme != "https"))
			throw new Failed (-1, "The URL is not valid: " + url);
		return r;
	}

	static void Add (HttpRequestMessage m, string k, string v)
	{
		if (m.Headers.TryAddWithoutValidation (k, v))
			return;
		m.Content ??= new ByteArrayContent (Array.Empty<byte> ());
		m.Content.Headers.TryAddWithoutValidation (k, v);
	}

	void Head (HttpRequestMessage m, Uri u)
	{
		var c = Resp.Console;
		c.Append (m.Method).Append (' ').Append (u).Append ("\r\n");
		c.Append (m.Method).Append (' ').Append (u.PathAndQuery).Append (" HTTP/1.1\r\nHost: ").Append (u.Authority).Append ("\r\n");
		void Line (string k, System.Net.Http.Headers.HeaderStringValues v) => c.Append (k).Append (": ").Append (v.ToString ()).Append ("\r\n");
		foreach (var h in m.Headers.NonValidated)
			Line (h.Key, h.Value);
		if (m.Content != null)
			foreach (var h in m.Content.Headers.NonValidated)
				Line (h.Key, h.Value);
		c.Append ("\r\n");
	}

	void BodyInConsole (byte[] b)
	{
		if (b.Length == 0)
			return;
		var c = Resp.Console;
		bool text = !b.Take (4096).Any (x => x < 9 || (x > 13 && x < 32));
		if (text)
		{
			c.Append (Encoding.UTF8.GetString (b, 0, Math.Min (b.Length, 8192)));
			if (b.Length > 8192)
				c.Append ("\n... (" + b.Length + " bytes in all)");
		}
		else
			c.Append ("(" + b.Length + " bytes of binary data)");
		c.Append ("\r\n\r\n");
	}

	static byte[] Inflate (byte[] b, bool gzip)
	{
		// (le deflate de HTTP est du zlib, mais des serveurs envoient du deflate nu)
		bool zlib = !gzip && b.Length >= 2 && (b[0] & 0x0F) == 8 && ((b[0] << 8) | b[1]) % 31 == 0;
		using var i = new MemoryStream (b);
		using Stream d = gzip ? new GZipStream (i, CompressionMode.Decompress) : zlib ? new ZLibStream (i, CompressionMode.Decompress) : new DeflateStream (i, CompressionMode.Decompress);
		using var o = new MemoryStream ();
		d.CopyTo (o);
		return o.ToArray ();
	}

	string Reasons (Uri u, HttpRequestException e)
	{
		for (Exception i = e; i != null; i = i.InnerException)
			if (i is AuthenticationException)
				return "The TLS handshake with " + u.Host + " failed: " + i.Message + (Prep.Insecure ? "" : " (Settings can turn off the certificate check.)");
		var s = (e.InnerException as SocketException) ?? (e.InnerException?.InnerException as SocketException);
		return "Could not connect to " + u.Host + ":" + u.Port + " (" + (s?.SocketErrorCode.ToString () ?? e.Message) + "): the host is unknown, refused the connection or did not answer.";
	}

	/// <summary>Un echange, sans redirection : les en-tetes dans Resp, le corps brut en retour.</summary>
	async Task<byte[]> Exchange (HttpClient http, string method, Uri u, List<KV> headers, byte[] body, bool first, Stopwatch t0)
	{
		using var m = new HttpRequestMessage (new HttpMethod (method), u) { Version = System.Net.HttpVersion.Version11, VersionPolicy = HttpVersionPolicy.RequestVersionExact };
		if (body.Length > 0 || method == "POST" || method == "PUT" || method == "PATCH")
			m.Content = new ByteArrayContent (body);
		foreach (var h in headers)
			if (!Util.EqI (h.Key, "Host") && !Util.EqI (h.Key, "Content-Length") && !Util.EqI (h.Key, "Connection"))
				Add (m, h.Key, h.Value);
		Head (m, u);
		BodyInConsole (body);
		// le delai court entre deux arrivees, comme dans le Courier d'Onyx
		using var quiet = new CancellationTokenSource (Prep.TimeoutMs);
		using var both = CancellationTokenSource.CreateLinkedTokenSource (_cancel.Token, quiet.Token);
		try
		{
			using var r = await http.SendAsync (m, HttpCompletionOption.ResponseHeadersRead, both.Token);
			if (first)
				Resp.MsFirst = (int) t0.ElapsedMilliseconds;
			Resp.Status = (int) r.StatusCode;
			Resp.Reason = r.ReasonPhrase ?? "";
			Resp.HttpVersion = "HTTP/" + r.Version;
			Resp.Headers.Clear ();
			var c = Resp.Console;
			c.Append (Resp.HttpVersion).Append (' ').Append (Resp.Status).Append (' ').Append (Resp.Reason).Append ("\r\n");
			long head = 0;
			void Keep (string k, System.Net.Http.Headers.HeaderStringValues vs)
			{
				foreach (var v in vs)
				{
					Resp.Headers.Add (new KV (k, v));
					c.Append (k).Append (": ").Append (v).Append ("\r\n");
					head += k.Length + v.Length + 4;
				}
			}
			foreach (var h in r.Headers.NonValidated)
				Keep (h.Key, h.Value);
			foreach (var h in r.Content.Headers.NonValidated)
				Keep (h.Key, h.Value);
			c.Append ("\r\n");
			Resp.HeadLen = head + 20;
			Resp.WireLen += Resp.HeadLen;
			using var o = new MemoryStream ();
			if (method != "HEAD")
			{
				using var s = await r.Content.ReadAsStreamAsync (both.Token);
				var buf = new byte[16384];
				for (;;)
				{
					quiet.CancelAfter (Prep.TimeoutMs);
					int n = await s.ReadAsync (buf, both.Token);
					if (n == 0)
						break;
					o.Write (buf, 0, Math.Min (n, RespMax - (int) o.Length));
					if (o.Length >= RespMax)
					{
						Resp.Truncated = true;
						break;
					}
				}
			}
			Resp.WireLen += o.Length;
			return o.ToArray ();
		}
		catch (OperationCanceledException)
		{
			if (_cancel.IsCancellationRequested)
				throw new Failed (-9, "Cancelled.");
			throw new Failed (-4, "No response within " + Prep.TimeoutMs + " ms (the timeout: the request's Settings).");
		}
		catch (HttpRequestException e)
		{
			throw new Failed (-5, Reasons (u, e));
		}
		catch (IOException e)
		{
			throw new Failed (-6, "The connection broke: " + e.Message);
		}
	}

	async Task<Stream> Connect (SocketsHttpConnectionContext ctx, CancellationToken ct, Stopwatch t0)
	{
		var s = new Socket (SocketType.Stream, ProtocolType.Tcp) { NoDelay = true };
		try
		{
			await s.ConnectAsync (ctx.DnsEndPoint, ct);
			if (Resp.MsConnect == 0)
				Resp.MsConnect = (int) t0.ElapsedMilliseconds;
			return new NetworkStream (s, true);
		}
		catch
		{
			s.Dispose ();
			throw;
		}
	}

	/// <summary>Toute la requete, redirections suivies si demande, dans Resp ; Jar prend les cookies.</summary>
	public async Task Run ()
	{
		var t0 = Stopwatch.StartNew ();
		var p = Prep;
		try
		{
			if (p.Error.Length > 0)
				throw new Failed (-10, p.Error);
			if (p.Url.Length == 0)
				throw new Failed (-1, "Enter a URL to send the request to.");
			using var handler = new SocketsHttpHandler
			{
				AllowAutoRedirect = false, UseCookies = false, UseProxy = false, AutomaticDecompression = System.Net.DecompressionMethods.None,
				ConnectCallback = (ctx, ct) => new ValueTask<Stream> (Connect (ctx, ct, t0)),
			};
			if (p.Insecure)
				handler.SslOptions = new SslClientAuthenticationOptions { RemoteCertificateValidationCallback = (s, c, ch, e) => true };
			using var http = new HttpClient (handler) { Timeout = System.Threading.Timeout.InfiniteTimeSpan };
			string method = p.Method;
			var headers = p.Headers.Select (h => h.Copy ()).ToList ();
			byte[] body = p.Body;
			var u = Parse (p.Url);
			for (int hop = 0; ; hop++)
			{
				byte[] raw = await Exchange (http, method, u, headers, body, hop == 0, t0);
				foreach (var h in Resp.Headers.Where (x => Util.EqI (x.Key, "Set-Cookie")))
				{
					var c = Jar.Set (h.Value, u);
					if (c != null)
						Resp.SetCookies.Add (c);
				}
				string loc = Resp.Header ("Location");
				bool redirect = Resp.Status >= 300 && Resp.Status < 400 && Resp.Status != 304 && !string.IsNullOrEmpty (loc);
				if (redirect && p.Follow && hop < 10 && Uri.TryCreate (u, loc, out var next))
				{
					Resp.Console.Append ("-> redirected (" + Resp.Status + ") to " + next + "\r\n\r\n");
					if (Resp.Status != 307 && Resp.Status != 308)
					{
						if (method != "HEAD")
							method = "GET";
						body = Array.Empty<byte> ();
					}
					u = next;
					Resp.Redirects++;
					// les cookies du nouvel hote, pas les anciens ; plus d'en-tete de corps en GET
					headers.RemoveAll (h => Util.EqI (h.Key, "Cookie") || (body.Length == 0 && Util.EqI (h.Key, "Content-Type")));
					string jar = Jar.Header (u);
					if (jar.Length > 0)
						headers.Add (new KV ("Cookie", jar));
					continue;
				}
				string ce = Resp.Header ("Content-Encoding");
				Resp.Body = raw;
				if (raw.Length > 0 && ce != null && (Util.EqI (ce, "gzip") || Util.EqI (ce, "x-gzip") || Util.EqI (ce, "deflate")))
					try { Resp.Body = Inflate (raw, !Util.EqI (ce, "deflate")); }
					catch (InvalidDataException) { }		// (montre tel quel)
				Resp.FinalUrl = u.ToString ();
				break;
			}
		}
		catch (Failed e)
		{
			Resp.Status = e.Code;
			Resp.Error = e.Message;
			Resp.Cancelled = e.Code == -9;
		}
		catch (Exception e) when (e is IOException || e is InvalidOperationException || e is ArgumentException || e is FormatException || e is CryptographicException)
		{
			Resp.Status = -7;
			Resp.Error = e.Message;
		}
		Resp.MsTotal = (int) t0.ElapsedMilliseconds;
	}
}
