//
// templates.h -- WebStudio : les fichiers des modèles de site (Nouveau site) et d'un fichier neuf.
// Un « %s » : le nom du site.
//
#ifndef _webstudio_templates_h
#define _webstudio_templates_h

namespace ws {

static const char T_EMPTY_INDEX[] =
	"<!doctype html>\n"
	"<html lang=\"fr\">\n"
	"<head>\n"
	"  <meta charset=\"utf-8\">\n"
	"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	"  <title>%s</title>\n"
	"</head>\n"
	"<body>\n"
	"  <h1>%s</h1>\n"
	"</body>\n"
	"</html>\n";

static const char T_PAGE_INDEX[] =
	"<!doctype html>\n"
	"<html lang=\"fr\">\n"
	"<head>\n"
	"  <meta charset=\"utf-8\">\n"
	"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	"  <title>%s</title>\n"
	"  <link rel=\"stylesheet\" href=\"css/style.css\">\n"
	"</head>\n"
	"<body>\n"
	"  <main>\n"
	"    <h1>%s</h1>\n"
	"    <p>Une page servie par Korin.</p>\n"
	"    <button id=\"bouton\">Cliquer</button>\n"
	"  </main>\n"
	"  <script src=\"js/app.js\"></script>\n"
	"</body>\n"
	"</html>\n";

static const char T_STYLE[] =
	"/* la feuille de style du site */\n"
	":root {\n"
	"  --fond: #FBF8F3;\n"
	"  --encre: #2B2522;\n"
	"  --accent: #C2562F;\n"
	"}\n"
	"\n"
	"body {\n"
	"  margin: 0;\n"
	"  background: var(--fond);\n"
	"  color: var(--encre);\n"
	"  font: 16px/1.5 sans-serif;\n"
	"}\n"
	"\n"
	"main, header, footer {\n"
	"  max-width: 960px;\n"
	"  margin: 0 auto;\n"
	"  padding: 24px;\n"
	"}\n"
	"\n"
	"a { color: var(--accent); }\n"
	"\n"
	"button {\n"
	"  background: var(--accent);\n"
	"  color: #FFFFFF;\n"
	"  border: 0;\n"
	"  border-radius: 4px;\n"
	"  padding: 8px 16px;\n"
	"}\n";

static const char T_SCRIPT[] =
	"// le script du site\n"
	"document.getElementById('bouton')?.addEventListener('click', () => {\n"
	"  document.querySelector('h1').textContent = 'Bonjour !';\n"
	"});\n";

// le site vitrine : un en-tête commun, trois pages, une page 404
static const char T_SHOP_HEAD[] =
	"<!doctype html>\n"
	"<html lang=\"fr\">\n"
	"<head>\n"
	"  <meta charset=\"utf-8\">\n"
	"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	"  <title>%s</title>\n"
	"  <link rel=\"stylesheet\" href=\"/css/style.css\">\n"
	"</head>\n"
	"<body>\n"
	"  <header>\n"
	"    <a href=\"/\"><strong>%s</strong></a>\n"
	"    <nav>\n"
	"      <a href=\"/\">Accueil</a>\n"
	"      <a href=\"/services.html\">Services</a>\n"
	"      <a href=\"/contact.html\">Contact</a>\n"
	"    </nav>\n"
	"  </header>\n"
	"  <main>\n";
static const char T_SHOP_FOOT[] =
	"  </main>\n"
	"  <footer>\n"
	"    <small>Servi par Korin.</small>\n"
	"  </footer>\n"
	"</body>\n"
	"</html>\n";

// un fichier neuf, selon son extension
static const char T_NEW_HTML[] =
	"<!doctype html>\n"
	"<html lang=\"fr\">\n"
	"<head>\n"
	"  <meta charset=\"utf-8\">\n"
	"  <title>%s</title>\n"
	"</head>\n"
	"<body>\n"
	"\n"
	"</body>\n"
	"</html>\n";

} // namespace ws

#endif
