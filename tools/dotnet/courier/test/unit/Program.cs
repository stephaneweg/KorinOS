// unit : le coeur de Courier sans fenetre, sur le PC (tools/dotnet/courier/test/unit.sh) : formats de Postman,
// variables, tests et captures, cURL, code, mise en forme, cookies.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

static class P
{
	static int ok, ko;

	static void Is (string name, object got, object want)
	{
		if (Equals (got?.ToString (), want?.ToString ()))
			ok++;
		else
		{
			ko++;
			Console.WriteLine ("KO " + name + "\n   attendu : " + want + "\n   recu    : " + got);
		}
	}
	static void Yes (string name, bool v) => Is (name, v, true);

	static string CollText (Collection c) => Encoding.UTF8.GetString (Json.Make (w => Postman.WriteCollection (w, c)));
	static Collection Read (string text)
	{
		using var d = JsonDocument.Parse (text, Json.Tolerant);
		return Postman.ReadCollection (d.RootElement);
	}

	static Response Resp (string body, int status = 200)
	{
		var r = new Response { Status = status, Reason = "OK", Body = Encoding.UTF8.GetBytes (body), MsTotal = 120 };
		r.Headers.Add (new KV ("Content-Type", "application/json; charset=utf-8"));
		r.SetCookies.Add (new Cookie { Name = "sid", Value = "abc" });
		return r;
	}

	static void Main (string[] a)
	{
		// Postman : une collection relue et reecrite ne change plus, et garde ce qui compte
		var c = Read (File.ReadAllText (a[0]));
		Is ("nom", c.Name, "Essai");
		Is ("requetes", c.Root.Kids.Count, 8);
		var post = c.Root.Kids[5];
		Is ("methode", post.Req.Method, "POST");
		Is ("bearer", post.Req.Auth.Type, AuthType.Bearer);
		Is ("jeton", post.Req.Auth.Token, "{{tok}}");
		Is ("corps", post.Req.BodyMode, BodyMode.Raw);
		Is ("langue", post.Req.RawLang, RawLang.Json);
		Is ("parametre", post.Req.Params.Single ().Key, "x");
		Is ("tests", c.Root.Kids[0].Req.Tests.Count, 3);
		Is ("captures", c.Root.Kids[0].Req.Captures[0].Value, "json.token");
		Yes ("non verifie", c.Root.Kids[1].Req.Insecure);
		string once = CollText (c), twice = CollText (Read (once));
		Is ("aller-retour", twice, once);
		Yes ("JavaScript de Postman", once.Contains ("pm.test(\\\"json.name armpi\\\"") && once.Contains ("pm.environment.set(\\\"tok\\\", pm.response.json().token);"));
		using (var d = JsonDocument.Parse (once))
		{
			var url = d.RootElement.Get ("item").Items ().ElementAt (5).Get ("request").Get ("url");
			Is ("protocole", url.Get ("protocol").Str (), "http");
			Is ("hote", string.Join (".", url.Get ("host").Items ().Select (x => x.Str ())), "10.0.2.2");
			Is ("port", url.Get ("port").Str (), "18765");
			Is ("chemin", url.Get ("path").Items ().Single ().Str (), "echo");
		}
		// des evenements inconnus sont gardes, un dossier aussi, avec son authentification
		var other = Read ("""
		{ "info": { "name": "B", "schema": "x" }, "item": [ { "name": "F", "auth": { "type": "basic", "basic": { "username": "u", "password": "p" } },
		  "item": [ { "name": "R", "event": [ { "listen": "prerequest", "script": { "exec": ["var a = 1;"] } } ], "request": "http://a/b" } ] } ],
		  "variable": [ { "key": "k", "value": "v" } ], "auth": { "type": "apikey", "apikey": [ { "key": "key", "value": "X" }, { "key": "value", "value": "Y" }, { "key": "in", "value": "query" } ] } }
		""");
		var f = other.Root.Kids[0];
		Yes ("dossier", f.Folder);
		Is ("v2.0", f.Auth.User, "u");
		Is ("requete en texte", f.Kids[0].Req.Url, "http://a/b");
		Yes ("apikey", other.Auth.Type == AuthType.ApiKey && other.Auth.InQuery);
		Yes ("script garde", CollText (other).Contains ("var a = 1;"));

		// variables
		var sc = new Scope { Env = new Env (), Globals = new Env (), Coll = new Collection () };
		sc.Env.Vars.Add (new KV ("a", "env"));
		sc.Coll.Vars.Add (new KV ("a", "coll"));
		sc.Coll.Vars.Add (new KV ("b", "coll-{{a}}"));
		sc.Globals.Vars.Add (new KV ("c", "glob"));
		sc.Globals.Vars.Add (new KV ("off", "x", false));
		Is ("environnement d'abord", sc.Resolve ("{{a}}"), "env");
		Is ("valeur a variable", sc.Resolve ("x{{b}}y"), "xcoll-envy");
		Is ("globale", sc.Resolve ("{{ c }}"), "glob");
		Is ("inconnue gardee", sc.Resolve ("{{nope}}/{{off}}"), "{{nope}}/{{off}}");
		Is ("accolades seules", sc.Resolve ("{a}} {{"), "{a}} {{");
		Yes ("dynamique", Guid.TryParse (sc.Resolve ("{{$guid}}"), out _));
		Yes ("courriel", sc.Resolve ("{{$randomEmail}}").EndsWith ("@example.com"));

		// preparer
		var q = new Request { Method = "post", Url = "{{c}}.test/x" };
		q.Headers.Add (new KV ("X-A", "{{a}}"));
		q.Headers.Add (new KV ("X-Off", "1", false));
		q.BodyMode = BodyMode.UrlEncoded;
		q.UrlEnc.Add (new KV ("k k", "v&v"));
		var p = Prepared.Make (q, sc, new Auth { Type = AuthType.Basic, User = "u", Pass = "p" }, new CookieJar ());
		Is ("url", p.Url, "http://glob.test/x");
		Is ("methode en capitales", p.Method, "POST");
		Is ("en-tete", p.Headers.First (h => h.Key == "X-A").Value, "env");
		Yes ("desactive", !p.Has ("X-Off"));
		Is ("basic", p.Headers.First (h => h.Key == "Authorization").Value, "Basic dTpw");
		Is ("urlencoded", Encoding.ASCII.GetString (p.Body), "k+k=v%26v");
		Is ("type", p.Headers.First (h => h.Key == "Content-Type").Value, "application/x-www-form-urlencoded");
		var k = Prepared.Make (new Request { Url = "http://h/p" }, sc, new Auth { Type = AuthType.ApiKey, Key = "key", Value = "a b", InQuery = true }, new CookieJar ());
		Is ("cle dans l'url", k.Url, "http://h/p?key=a%20b");
		var m = new Request { Url = "http://h/", BodyMode = BodyMode.Form };
		m.Form.Add (new KV ("f", "1"));
		string mp = Encoding.UTF8.GetString (Prepared.Make (m, sc, new Auth { Type = AuthType.None }, new CookieJar ()).Body);
		Yes ("multipart", mp.StartsWith ("------CourierFormBoundary") && mp.Contains ("name=\"f\"\r\n\r\n1\r\n"));

		// tests
		using (var ev = new Eval (Resp ("{\"a\":{\"b\":[1,{\"c\":\"x\"}]},\"n\":2.5,\"nul\":null,\"d.e\":true}")))
		{
			Is ("chemin json", ev.Get ("json.a.b[1].c"), "x");
			Is ("$", ev.Get ("$.a.b[0]"), "1");
			Is ("[-1]", ev.Get ("json.a.b[-1].c"), "x");
			Is ("cle entre guillemets", ev.Get ("json[\"d.e\"]"), "true");
			Is ("null", ev.Get ("json.nul"), "null");
			Is ("absent", ev.Get ("json.zz"), null);
			Is ("en-tete", ev.Get ("header.content-type"), "application/json; charset=utf-8");
			Is ("cookie", ev.Get ("cookie.sid"), "abc");
			foreach (var (chk, exp, want) in new[] { ("status", "200", true), ("status", "= 201", false), ("status", "!= 201", true), ("status", "< 500", true), ("status", ">= 300", false),
				("json.n", "2.50", true), ("header.Content-Type", "contains json", true), ("body", "matches \"c\"", true), ("json.a", "exists", true), ("json.zz", "", false), ("json.zz", "not exists", true), ("json.n", "> abc", false) })
				Is ("test " + chk + " " + exp, ev.Test (chk, exp, out _), want);
		}
		using (var ev = new Eval (Resp ("pas du json")))
			Is ("pas du JSON", ev.Get ("json.a"), null);

		// cURL
		var cu = Curl.Parse ("curl -X PUT 'https://h.test/a?x=1&y=2' -H 'Accept: text/plain' \\\n -H \"X-Q: \\\"q\\\"\" --data-raw '{\"a\":1}' -H 'Content-Type: application/json' -u me:pw --compressed", out string err);
		Is ("erreur", err, "");
		Is ("methode", cu.Method, "PUT");
		Is ("url", cu.Url, "https://h.test/a?x=1&y=2");
		Is ("params", cu.Params.Count, 2);
		Is ("en-tetes", string.Join ("|", cu.Headers.Select (h => h.Key + "=" + h.Value)), "Accept=text/plain|X-Q=\"q\"");
		Is ("brut", cu.Raw, "{\"a\":1}");
		Is ("json", cu.RawLang, RawLang.Json);
		Is ("basic", cu.Auth.User + ":" + cu.Auth.Pass, "me:pw");
		Is ("nom", cu.Name, "h.test/a");
		var cf = Curl.Parse ("curl http://h/ -d 'a=1&b=x%20y'", out _);
		Is ("POST implicite", cf.Method, "POST");
		Is ("urlencoded", cf.BodyMode, BodyMode.UrlEncoded);
		Is ("decode", cf.UrlEnc[1].Value, "x y");
		var cm = Curl.Parse ("curl http://h/ -F a=1 -F 'f=@/tmp/x.png;type=image/png'", out _);
		Yes ("fichier", cm.BodyMode == BodyMode.Form && cm.Form[1].File && cm.Form[1].Value == "/tmp/x.png");
		Is ("pas curl", Curl.Parse ("wget x", out _), null);
		// le code cURL se relit
		var back = Curl.Parse (Snippet.Make (0, cu, new Scope (), cu.Auth), out _);
		Is ("retour url", back.Url, cu.Url);
		Is ("retour methode", back.Method, "PUT");
		Is ("retour corps", back.Raw, cu.Raw);
		for (int i = 1; i < Snippet.Names.Length; i++)
			Yes ("code " + Snippet.Names[i], Snippet.Make (i, cu, new Scope (), cu.Auth).Contains ("h.test"));

		// mise en forme
		Is ("json", Pretty.Json ("{\"a\":[1,2],\"b\":{},\"s\":\"x,y:{\"}"), "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": {},\n  \"s\": \"x,y:{\"\n}\n");
		Is ("pas json", Pretty.Json ("hello"), null);
		Is ("json coupe", Pretty.Json ("{\"a\":1"), null);
		Is ("xml", Pretty.Xml ("<?xml version=\"1.0\"?><a><b>t</b><c x=\"1\"/></a>"), "<?xml version=\"1.0\"?>\n<a>\n  <b>t</b>\n  <c x=\"1\"/>\n</a>\n");
		Yes ("html", Pretty.HtmlText ("<html><head><title>T</title></head><body><p>a &amp; b</p><script>x</script><li>i</li></body></html>").Contains ("a & b"));

		// cookies
		var jar = new CookieJar ();
		var u = new Uri ("https://a.example.com/x/y");
		jar.Set ("s=1; Path=/; Domain=.example.com; Secure", u);
		jar.Set ("t=2; Max-Age=3600", u);
		jar.Set ("old=3; Max-Age=0", u);
		Is ("https", jar.Header (u), "s=1; t=2");
		Is ("http : pas le secure", jar.Header (new Uri ("http://a.example.com/x/z")), "t=2");
		Is ("sous-domaine", jar.Header (new Uri ("https://b.example.com/")), "s=1");
		Is ("autre domaine", jar.Header (new Uri ("https://example.org/")), "");
		jar.Set ("t=; Max-Age=0", u);
		Is ("retire", jar.Header (u), "s=1");

		Console.WriteLine (ok + " ok, " + ko + " KO");
		Environment.Exit (ko == 0 ? 0 : 1);
	}
}
