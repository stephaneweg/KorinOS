// Maquettes de l'écran de connexion : glyphes, fond à facettes, image d'un compte, choix du thème, zoom.
// Glyphes au trait de 1,5, bouts ronds, grille de 24 (comme glyph-gear et glyph-power, docs/DESIGN-PROPOSAL.md §4).
(function () {
	var G = {
		power: '<path d="M12 3v8"/><path d="M6.3 6.3a8 8 0 1 0 11.4 0"/>',
		clavier: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6.5 10h.01M10 10h.01M13.5 10h.01M17 10h.01M7 14h10"/>',
		access: '<circle cx="12" cy="4.5" r="1.6"/><path d="M4.5 8.5 12 10l7.5-1.5M12 10v4.5M8.5 21 12 14.5 15.5 21"/>',
		wifi: '<path d="M2.5 9a14 14 0 0 1 19 0M5.5 12.5a9.5 9.5 0 0 1 13 0M8.6 16a5 5 0 0 1 6.8 0"/><path d="M12 19.5h.01"/>',
		reseau: '<rect x="9" y="3" width="6" height="5" rx="1"/><rect x="3" y="16" width="6" height="5" rx="1"/><rect x="15" y="16" width="6" height="5" rx="1"/><path d="M12 8v4M6 16v-4h12v4"/>',
		fleche: '<path d="M5 12h14M13 6l6 6-6 6"/>',
		oeil: '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
		verrou: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>',
		maj: '<path d="M12 4 19 12h-4v5H9v-5H5z"/><path d="M9 20.5h6"/>',
		changer: '<circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><path d="M16 4.6a3.5 3.5 0 0 1 0 6.8M18 14.2a6.5 6.5 0 0 1 3.5 5.8"/>',
		repos: '<path d="M20 14.5A8.5 8.5 0 1 1 9.5 4 6.5 6.5 0 0 0 20 14.5z"/>',
		redem: '<path d="M20 12a8 8 0 1 1-2.3-5.7"/><path d="M20 4v5h-5"/>',
		ok: '<path d="M5 12.5 9.5 17 19 7"/>',
		personne: '<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/>',
		batterie: '<rect x="2.5" y="7" width="17" height="10" rx="2"/><path d="M22 10.5v3"/><path d="M5.5 10v4M8.5 10v4M11.5 10v4" />',
		bas: '<path d="M6 9l6 6 6-6"/>',
		effacer: '<path d="M9 5h11a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H9l-6-7z"/><path d="M12 9.5l5 5M17 9.5l-5 5"/>',
		sortir: '<path d="M14 4h4.5a1.5 1.5 0 0 1 1.5 1.5v13a1.5 1.5 0 0 1-1.5 1.5H14"/><path d="M10 8l-4 4 4 4M6 12h10"/>',
		info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 7.5h.01"/>',
		alerte: '<path d="M12 3.5 21.5 20h-19z"/><path d="M12 10v4.5M12 17.5h.01"/>',
		texte: '<path d="M4 7V5h11v2M9.5 5v14M7.5 19h4"/><path d="M14 12v-1.5h6V12M17 10.5V19M15.8 19h2.4"/>',
		contraste: '<circle cx="12" cy="12" r="9"/><path d="M12 3v18a9 9 0 0 0 0-18z" fill="currentColor"/>',
		haut: '<path d="M7 17V7h10"/><path d="M7 7l10 10"/>',
		globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.6 3.5 5.6 3.5 9s-1 6.4-3.5 9c-2.5-2.6-3.5-5.6-3.5-9s1-6.4 3.5-9z"/>',
		pc: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
	};
	var sym = '';
	for (var k in G)
		sym += '<symbol id="g-' + k + '" viewBox="0 0 24 24">' + G[k] + '</symbol>';

	// Le fond à facettes : 12 facettes ton sur ton (les cinq couleurs du fond, liseré clair), comme Elegant.
	var P = [
		['0,0 230,0 300,210 0,280', 1], ['230,0 540,0 500,300 300,210', 3], ['540,0 800,0 760,190 500,300', 2],
		['800,0 1024,0 1024,260 760,190', 5], ['0,280 300,210 210,470 0,560', 4], ['300,210 500,300 560,540 210,470', 2],
		['500,300 760,190 820,450 560,540', 4], ['760,190 1024,260 1024,600 820,450', 1], ['0,560 210,470 280,768 0,768', 2],
		['210,470 560,540 470,768 280,768', 5], ['560,540 820,450 850,768 470,768', 1], ['820,450 1024,600 1024,768 850,768', 3],
	];
	var fond = '<symbol id="fond" viewBox="0 0 1024 768" preserveAspectRatio="none">';
	P.forEach(function (p) {
		fond += '<polygon points="' + p[0] + '" style="fill:var(--f' + p[1] + ');stroke:var(--lisere);stroke-width:1"/>';
	});
	fond += '</symbol>';

	// Image du compte de Stéphane (picture = ~/.face.png) : une photo, elle garde ses couleurs.
	var photo = '<symbol id="photo" viewBox="0 0 64 64"><defs><linearGradient id="ciel" x1="0" y1="0" x2="0" y2="1">' +
		'<stop offset="0" stop-color="#F6B26B"/><stop offset="1" stop-color="#E06C5A"/></linearGradient></defs>' +
		'<rect width="64" height="64" fill="url(#ciel)"/><circle cx="42" cy="22" r="7" fill="#FFE3A8"/>' +
		'<path d="M0 46 16 30l12 12 10-8 26 18v12H0z" fill="#5B4A7A"/><path d="M0 54l18-10 14 8 12-6 20 10v8H0z" fill="#3A3157"/></symbol>';

	document.body.insertAdjacentHTML('afterbegin',
		'<svg width="0" height="0" style="position:absolute" aria-hidden="true">' + sym + fond + photo + '</svg>');

	// <i data-g="power"></i> devient le glyphe ; data-c ajoute une classe de taille
	document.querySelectorAll('i[data-g]').forEach(function (i) {
		var c = 'g' + (i.dataset.c ? ' ' + i.dataset.c : '');
		i.outerHTML = '<svg class="' + c + '"><use href="#g-' + i.dataset.g + '"/></svg>';
	});
	document.querySelectorAll('i[data-fond]').forEach(function (i) {
		i.outerHTML = '<svg class="fond"><use href="#fond"/></svg>';
	});
	document.querySelectorAll('i[data-photo]').forEach(function (i) {
		i.outerHTML = '<svg class="photo" viewBox="0 0 64 64"><use href="#photo"/></svg>';
	});

	// thème : ?theme=noir, sinon le dernier choix, sinon celui du système
	var root = document.documentElement, t = null;
	var q = /[?&]theme=(givre|noir)/.exec(location.search);
	if (q) t = q[1];
	else {
		try { t = localStorage.getItem('korin-maquette-theme'); } catch (e) {}
		if (!t && window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches) t = 'noir';
	}
	function pose(v) {
		root.dataset.theme = v;
		document.querySelectorAll('.seg button').forEach(function (b) { b.classList.toggle('on', b.dataset.t === v); });
		try { localStorage.setItem('korin-maquette-theme', v); } catch (e) {}
	}
	pose(t || 'givre');
	document.querySelectorAll('.seg button').forEach(function (b) {
		b.addEventListener('click', function () { pose(b.dataset.t); });
	});
	document.querySelectorAll('.cadre').forEach(function (c) {
		c.addEventListener('click', function () { c.classList.toggle('grand'); });
	});
})();
