// frame-probe.js -- une sonde pour SD0:/etc/web-probe.js (docs/WEBKIT.md, « hCaptcha ») : Jet la lance dans chaque
// cadre de chaque page (les iframes de hCaptcha comprises) avant leurs scripts. Avec SD0:/etc/web-console, le journal du
// noyau reçoit pour chaque cadre ce qu'il voit du navigateur et ses erreurs JavaScript (ce qui casse une vérification).
//   cp SD0:/webkit/frame-probe.js SD0:/etc/web-probe.js ; touch SD0:/etc/web-console ; (relancer Jet)
(function () {
	var where = location.host + location.pathname.substring(0, 40) + (window.top === window ? " (page)" : " (iframe)");
	function say(s) { try { console.log("probe " + where + ": " + s); } catch (e) {} }
	var n = navigator;
	say("platform=" + n.platform + " vendor=" + n.vendor + " subtle=" + !!(window.crypto && crypto.subtle) + " secure=" + window.isSecureContext +
		" cookies=" + n.cookieEnabled + " touch=" + n.maxTouchPoints + " cores=" + n.hardwareConcurrency);
	try { localStorage.setItem("__probe", "1"); say("localStorage ok"); } catch (e) { say("localStorage REFUSE : " + e); }
	if (document.hasStorageAccess)
		document.hasStorageAccess().then(function (a) { say("hasStorageAccess=" + a); }, function (e) { say("hasStorageAccess : " + e); });
	window.addEventListener("error", function (e) { say("ERREUR " + e.message + " (" + (e.filename || "").substring(0, 80) + ":" + e.lineno + ")"); });
	window.addEventListener("unhandledrejection", function (e) { say("PROMESSE REJETEE " + (e.reason && (e.reason.message || e.reason))); });
	window.addEventListener("message", function (e) { say("message de " + e.origin + " (" + String(typeof e.data === "string" ? e.data : JSON.stringify(e.data)).substring(0, 80) + ")"); });
})();
