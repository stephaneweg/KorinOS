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
	function pwd(v) {
		var m = /^#PWD_[A-Z]+:(\d+):(\d+):(.*)$/.exec(v);
		if (!m) return "pas au format #PWD_ (" + v.length + " caractères)";
		var d = m[2] - Math.floor(Date.now() / 1000);
		return "#PWD version " + m[1] + ", horodatage " + m[2] + " (" + (d >= 0 ? "+" : "") + d + " s), " + m[3].length + " caractères"
			+ (m[1] === "0" ? " : VERSION 0 = mot de passe EN CLAIR, le chiffrement a échoué" : "");
	}
	function fields(names, get) {
		var out = [];
		for (var i = 0; i < names.length; i++) {
			var n = names[i];
			out.push(/pass/i.test(n) ? n + "=[" + pwd(String(get(n) || "")) + "]" : n);
		}
		return out.join(" ");
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
					function (e) { say("subtle." + f + " " + what + " ÉCHEC " + (e && (e.name + ": " + e.message))); throw e; });
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
			var url = addr(u && u.url ? u.url : u);
			say("fetch " + m + " " + url + " : " + body(o && o.body));
			return of.apply(this, arguments).then(function (r) { say("fetch " + url + " -> " + r.status); return r; },
				function (e) { say("fetch " + url + " ÉCHEC " + e); throw e; });
		}
		return of.apply(this, arguments);
	};
	var xo = XMLHttpRequest.prototype.open, xs = XMLHttpRequest.prototype.send;
	XMLHttpRequest.prototype.open = function (m, u) { this.__probe = [m, addr(u)]; return xo.apply(this, arguments); };
	XMLHttpRequest.prototype.send = function (b) {
		var p = this.__probe;
		if (p && String(p[0]).toUpperCase() !== "GET") {
			say("XHR " + p[0] + " " + p[1] + " : " + body(b));
			this.addEventListener("loadend", function () { say("XHR " + p[1] + " -> " + this.status); });
		}
		return xs.apply(this, arguments);
	};
	var sb = navigator.sendBeacon;
	if (sb) navigator.sendBeacon = function (u, d) { say("sendBeacon " + addr(u) + " : " + body(d)); return sb.apply(navigator, arguments); };
})();
