// login-probe.js -- une sonde pour /etc/web-probe.js (docs/WEBKIT.md, « Connexion à Facebook ») : ce que fait le
// script d'une page de connexion entre le clic sur « Se connecter » et la réponse. Avec /etc/web-console, le journal
// du noyau reçoit : l'heure de la machine, les erreurs JavaScript et promesses rejetées, chaque appel de crypto.subtle
// (algorithme, tailles, réussite ou erreur), les envois de formulaire et les POST de fetch / XMLHttpRequest (adresse
// sans la requête, taille du corps, noms des champs). Les VALEURS des champs ne sont jamais écrites : du mot de passe
// chiffré de Facebook (#PWD_BROWSER:<version>:<horodatage>:<base64>) seuls la version, l'horodatage et la taille.
//   cp /apps/jet.app/login-probe.js /etc/web-probe.js ; touch /etc/web-console ; (relancer Jet)
(function () {
	var where = location.host + location.pathname.substring(0, 30) + (window.top === window ? "" : " (iframe)");
	function say(s) { try { console.log("login " + where + ": " + s); } catch (e) {} }
	function size(b) { try { return b == null ? 0 : b.byteLength != null ? b.byteLength : b.length != null ? b.length : -1; } catch (e) { return -1; } }
	function algo(a) {
		if (typeof a === "string") return a;
		if (!a) return "?";
		var s = a.name || "?";
		if (a.length) s += " " + a.length;
		if (a.namedCurve) s += " " + a.namedCurve;
		if (a.iv) s += " iv=" + size(a.iv);
		if (a.additionalData) s += " aad=" + size(a.additionalData);
		if (a.tagLength) s += " tag=" + a.tagLength;
		if (a.hash) s += " hash=" + (a.hash.name || a.hash);
		return s;
	}
	// Ce que dit un champ chiffré (#PWD_BROWSER:5:1731900000:AbC...) : la version, l'horodatage et sa différence
	// avec l'horloge de la machine, la taille ; jamais la valeur.
	// La charge d'un #PWD_BROWSER:5 (base64) : [1 octet, 1][numéro de la clé publique][longueur de la clé scellée,
	// 2 octets petit-boutiste][clé scellée : 32 + 48 = 80][étiquette GCM 16][texte chiffré] -- sa forme, pas son contenu
	function payload(b64) {
		try {
			var s = atob(b64), n = s.length, k = s.charCodeAt(2) | (s.charCodeAt(3) << 8);
			return "charge " + n + " octets : octet0=" + s.charCodeAt(0) + " clé n°" + s.charCodeAt(1) + " scellée=" + k
				+ " reste " + (n - 4 - k) + " (16 d'étiquette + " + (n - 4 - k - 16) + " de mot de passe)";
		} catch (e) { return "charge illisible : " + e; }
	}
	function pwd(v) {
		var m = /^#PWD_[A-Z]+:(\d+):(\d+):(.*)$/.exec(v);
		if (!m) return "pas au format #PWD_ (" + v.length + " caractères)";
		var d = m[2] - Math.floor(Date.now() / 1000);
		return "#PWD version " + m[1] + ", horodatage " + m[2] + " (" + (d >= 0 ? "+" : "") + d + " s), " + m[3].length + " caractères"
			+ (m[1] === "0" ? " : VERSION 0 = mot de passe EN CLAIR, le chiffrement a échoué" : " ; " + payload(m[3]));
	}
	// les champs « mot de passe » dans un JSON (le champ variables des requêtes graphql de Facebook)
	function jsonPwd(text) {
		var out = [];
		try {
			(function walk(o, path) {
				if (o && typeof o === "object")
					for (var k in o) {
						var v = o[k];
						if (typeof v === "string" && (/pass/i.test(k) || /^#PWD_/.test(v))) out.push(path + k + "=[" + pwd(v) + "]");
						else if (typeof v === "string" && v.charAt(0) === "{") { try { walk(JSON.parse(v), path + k + "."); } catch (e) {} }
						else walk(v, path + k + ".");
					}
			})(JSON.parse(text), "");
		} catch (e) {}
		return out.join(" ");
	}
	function mask(s) { return String(s).replace(/[^\s"'<>@]+@[^\s"'<>@]+/g, "<adresse>").replace(/\+?\d[\d .-]{7,}\d/g, "<numéro>"); }
	function fields(names, get) {
		var out = [];
		for (var i = 0; i < names.length; i++) {
			var n = names[i];
			if (/pass/i.test(n)) out.push(n + "=[" + pwd(String(get(n) || "")) + "]");
			else if (n === "variables") { var j = jsonPwd(String(get(n) || "")); out.push(j ? n + "{" + j + "}" : n); }
			else out.push(n);
		}
		return out.join(" ");
	}
	// une requête qui portait un mot de passe : la réponse dit pourquoi elle est refusée (l'erreur, adresses masquées)
	function hasPwd(desc) { return /#PWD|pass[a-z]*=\[/i.test(desc); }
	function answer(text) {
		text = String(text || "");
		var i = text.search(/"(errors?|error_code|error_msg|description|summary|message|login_failure|err)"/);
		return mask(i < 0 ? text.substring(0, 300) : text.substring(Math.max(0, i - 20), i + 400)).replace(/\s+/g, " ");
	}
	function body(b) {
		try {
			if (typeof b === "string") {
				var names = [], p = b.split("&");
				for (var i = 0; i < p.length; i++) names.push(decodeURIComponent(p[i].split("=")[0]));
				var get = function (n) {
					for (var j = 0; j < p.length; j++) {
						var kv = p[j].split("=");
						if (decodeURIComponent(kv[0]) === n) return decodeURIComponent((kv[1] || "").replace(/\+/g, " "));
					}
				};
				return b.length + " octets : " + fields(names, get);
			}
			if (b instanceof FormData || b instanceof URLSearchParams) {
				var ns = [];
				b.forEach(function (v, k) { if (ns.indexOf(k) < 0) ns.push(k); });
				return (b instanceof FormData ? "FormData" : "URLSearchParams") + " : " + fields(ns, function (n) { return b.get(n); });
			}
			return size(b) + " octets";
		} catch (e) { return "(illisible : " + e + ")"; }
	}
	function addr(u) { u = String(u); return u.split("?")[0].substring(0, 100); }

	say("ouverte ; heure de la machine " + new Date().toISOString() + " (décalage " + new Date().getTimezoneOffset() + " min)"
		+ " ; subtle=" + !!(window.crypto && crypto.subtle) + " sûre=" + window.isSecureContext
		+ " BigInt=" + (typeof BigInt) + " TextEncoder=" + (typeof TextEncoder));
	window.addEventListener("error", function (e) { say("ERREUR " + e.message + " (" + (e.filename || "").substring(0, 80) + ":" + e.lineno + ":" + e.colno + ")"); });
	window.addEventListener("unhandledrejection", function (e) {
		var r = e.reason;
		say("PROMESSE REJETÉE " + (r && (r.name ? r.name + ": " : "") + (r.message || r)) + (r && r.stack ? " | " + String(r.stack).split("\n")[0].substring(0, 120) : ""));
	});

	// Ce qu'est un argument de crypto.subtle (jamais son contenu) : sa sorte, sa taille, son décalage dans son tampon,
	// le tampon (partagé, redimensionnable, détaché) et le monde (realm) qui l'a fait.
	function shape(b) {
		try {
			if (b == null) return String(b);
			var buf = b.buffer || b, s = Object.prototype.toString.call(b).slice(8, -1);
			if (b.constructor && b.constructor !== window[b.constructor.name]) s += " (autre realm)";
			if (b.byteOffset != null) s += " +" + b.byteOffset;
			s += " " + size(b) + "/" + size(buf);
			if (buf instanceof ArrayBuffer || Object.prototype.toString.call(buf) === "[object ArrayBuffer]") {
				if (buf.resizable) s += " redimensionnable";
				if (buf.detached) s += " DÉTACHÉ";
			} else s += " tampon " + Object.prototype.toString.call(buf).slice(8, -1);
			return s;
		} catch (e) { return "(illisible : " + e + ")"; }
	}
	function keyShape(k) {
		try { return "clé " + k.type + " " + (k.algorithm && k.algorithm.name) + " " + (k.algorithm && k.algorithm.length) + " extractible=" + k.extractable + " usages=" + k.usages.join(","); }
		catch (e) { return "(clé illisible : " + e + ")"; }
	}
	function copy(b) { var v = b.buffer ? new Uint8Array(b.buffer, b.byteOffset, b.byteLength) : new Uint8Array(b); return new Uint8Array(v); }
	// encrypt a échoué : les arguments, puis le même appel refait avec des copies propres, un argument à la fois
	function retry(S, orig, a) {
		var p = a[0] || {}, k = a[1], d = a[2];
		say("encrypt en échec : params " + Object.keys(p).join(",") + " ; iv " + shape(p.iv) + " ; aad " + shape(p.additionalData)
			+ " ; données " + shape(d) + " ; " + keyShape(k) + " ; même realm que la page : " + (p.constructor === Object));
		function again(name, pp, kk, dd) {
			return Promise.resolve().then(function () { return orig.call(S, pp, kk, dd); })
				.then(function (r) { say("  refait (" + name + ") -> " + r.byteLength + " octets"); }, function (e) { say("  refait (" + name + ") ÉCHEC " + (e && e.name + ": " + e.message)); });
		}
		var clean = { name: "AES-GCM", iv: copy(p.iv) };
		if (p.additionalData) clean.additionalData = copy(p.additionalData);
		if (p.tagLength) clean.tagLength = p.tagLength;
		return again("tel quel", p, k, d)
			.then(function () { return again("params copiés", clean, k, d); })
			.then(function () { return again("données copiées", p, k, copy(d)); })
			.then(function () { return again("tout copié", clean, k, copy(d)); })
			.then(function () {
				return S.generateKey({ name: "AES-GCM", length: 256 }, true, ["encrypt", "decrypt"])
					.then(function (k2) { return again("tout copié, clé neuve", clean, k2, copy(d)); });
			});
	}

	// crypto.subtle : chaque appel et son issue
	if (window.crypto && crypto.subtle) {
		var S = crypto.subtle;
		["encrypt", "decrypt", "importKey", "exportKey", "generateKey", "digest", "sign", "deriveBits", "deriveKey", "wrapKey"].forEach(function (f) {
			var orig = S[f];
			if (!orig) return;
			S[f] = function () {
				var a = arguments, what;
				if (f === "importKey" || f === "exportKey") what = a[0] + " " + (f === "importKey" ? algo(a[2]) + " données=" + size(a[1]) : "");
				else if (f === "generateKey" || f === "digest") what = algo(a[0]);
				else what = algo(a[0]) + " données=" + size(a[a.length - 1]);
				var p;
				try { p = orig.apply(S, a); } catch (e) { say("subtle." + f + " " + what + " LÈVE " + e); throw e; }
				return p.then(function (r) { say("subtle." + f + " " + what + " -> " + (r && r.byteLength != null ? r.byteLength + " octets" : r && r.type ? "clé " + r.type : "ok")); return r; },
					function (e) {
						say("subtle." + f + " " + what + " ÉCHEC " + (e && (e.name + ": " + e.message)));
						if (f === "encrypt") retry(S, orig, a);
						throw e;
					});
			};
		});
	} else
		say("crypto.subtle ABSENT");
	var gr = window.crypto && crypto.getRandomValues;
	if (gr) crypto.getRandomValues = function (a) { try { return gr.call(crypto, a); } catch (e) { say("getRandomValues(" + size(a) + ") LÈVE " + e); throw e; } };

	// les formulaires : l'envoi par l'utilisateur (événement submit) et par script (form.submit())
	function form(f, how) {
		try {
			var ns = [];
			for (var i = 0; i < f.elements.length; i++) if (f.elements[i].name) ns.push(f.elements[i].name);
			say("formulaire " + how + " " + (f.method || "get").toUpperCase() + " " + addr(f.action) + " : " + fields(ns, function (n) {
				var e = f.elements[n]; return e && (e.value != null ? e.value : e[0] && e[0].value);
			}));
		} catch (e) { say("formulaire " + how + " (illisible : " + e + ")"); }
	}
	document.addEventListener("submit", function (e) { form(e.target, "envoyé" + (e.defaultPrevented ? " (empêché par la page)" : "")); }, true);
	var fs = HTMLFormElement.prototype.submit;
	HTMLFormElement.prototype.submit = function () { form(this, "submit() par script"); return fs.apply(this, arguments); };

	// les POST du script
	var of = window.fetch;
	if (of) window.fetch = function (u, o) {
		var m = (o && o.method) || (u && u.method) || "GET";
		if (String(m).toUpperCase() !== "GET") {
			var url = addr(u && u.url ? u.url : u), desc = body(o && o.body);
			say("fetch " + m + " " + url + " : " + desc);
			var withPwd = hasPwd(desc);
			return of.apply(this, arguments).then(function (r) {
				if (withPwd) r.clone().text().then(function (t) { say("fetch " + url + " -> " + r.status + " ; réponse : " + answer(t)); }, function () {});
				else say("fetch " + url + " -> " + r.status);
				return r;
			},
				function (e) { say("fetch " + url + " ÉCHEC " + e); throw e; });
		}
		return of.apply(this, arguments);
	};
	var xo = XMLHttpRequest.prototype.open, xs = XMLHttpRequest.prototype.send;
	XMLHttpRequest.prototype.open = function (m, u) { this.__probe = [m, addr(u)]; return xo.apply(this, arguments); };
	XMLHttpRequest.prototype.send = function (b) {
		var p = this.__probe;
		if (p && String(p[0]).toUpperCase() !== "GET") {
			var desc = body(b);
			say("XHR " + p[0] + " " + p[1] + " : " + desc);
			var withPwd = hasPwd(desc);
			this.addEventListener("loadend", function () {
				var r = "";
				if (withPwd) try { r = " ; réponse : " + answer(this.responseType === "" || this.responseType === "text" ? this.responseText : "(" + this.responseType + ")"); } catch (e) { r = " ; réponse illisible : " + e; }
				say("XHR " + p[1] + " -> " + this.status + r);
			});
		}
		return xs.apply(this, arguments);
	};
	var sb = navigator.sendBeacon;
	if (sb) navigator.sendBeacon = function (u, d) { say("sendBeacon " + addr(u) + " : " + body(d)); return sb.apply(navigator, arguments); };
})();
