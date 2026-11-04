# PocketQI

L'assistante de Korin OS, portée d'Onyx (`user/Apps/pocketqi`), d'après le projet PocketQI de l'utilisateur. On lui pose une
question **par écrit** sur le système ou sur une application, la bureautique surtout ; elle répond dans une bulle, comme le
compagnon d'Office, et peut ouvrir l'application dont il est question. Pas d'écoute : ni micro, ni mot d'éveil.

    pocketqi                  une fenêtre ordinaire (lanceur, catégorie Accessoires)
    pocketqi --popup <x>      la bulle de la barre de menus, sous l'icône d'abscisse x

**L'applet** : `src/sys/menubar/menubar.asm` dessine à gauche du haut-parleur une petite bulle avec les deux yeux de Pocket,
si `SD0:/apps/pocketqi.app/main` existe. Un clic lance `pocketqi --popup <x>` ; un second clic lance une seconde bulle, qui
trouve la première par `RAM0:/pocketqi.pid` et la liste des process, l'arrête et s'arrête. Échap ferme aussi la bulle.

## Les compétences : les manifestes

Ce que PocketQI sait vient des `app.txt` de `SD0:/apps/*.app` (relus toutes les 5 minutes) et de `korin.txt` (le bureau lui-même,
`data/korin.txt`). Le format est décrit dans [docs/ABI.md](../../../docs/ABI.md) : aux clés d'Onyx (`name`, `description`,
`llm.<action>`) s'ajoutent `keywords`, `help` (un conseil par ligne) et `run.<action>`. Toutes les apps de `src/apps` les ont.

Pour écrire les compétences d'une nouvelle app : une `description` d'une phrase, des `keywords` que quelqu'un emploierait pour la
désigner, et des `help` vérifiés dans ses menus (« menu Fichier > Exporter en PDF... »), jamais inventés. Pas de `#` dans une
valeur. Une action `llm.*` seulement si l'app comprend vraiment ces arguments.

## Une question

`brain.h`, sur un fil à elle (la fenêtre ne l'attend jamais) :

1. le prompt système : le rôle (aider à utiliser Korin OS, réponses brèves, rien d'inventé), les outcomes permis, la date, le
   bureau (`korin.txt`), puis **chaque application en une ligne** (id, nom, catégorie, description, actions) ; les trois les plus
   proches de la question (par les mots de `keywords`, du nom, de la description) y ont aussi tous leurs conseils ;
2. les 6 derniers échanges (oubliés après 10 minutes sans question) et la question ;
3. le service répond en JSON `{"outcomes": [{"app": ..., "args": [...]}]}`, comme PocketQI : `humeur` (le visage), `say` (la
   bulle), `aide [app]` (les conseils d'une autre app : second tour), ou une app de la liste et son action, **vérifiée contre son
   manifeste** (action connue, nombre et format des arguments) avant d'être lancée. Un appel refusé repart au modèle avec la
   raison ; deux seconds tours au plus.

**Sans service** (rien de réglé, « Personne » choisi, ou le service ne répond pas) : la réponse locale, tirée des mêmes manifestes :
l'app la plus proche de la question, sa description et ses conseils les plus proches ; « ouvre » ou « lance » l'ouvre.

## Les outils : ce qu'elle fait elle-même

Quatre outils, faits par PocketQI au lieu d'être seulement décrits (`tools.h`). Chacun est déclaré comme une application, dans un
manifeste au même format (`data/tools/<id>.txt`, posé dans `SD0:/apps/pocketqi.app/tools` : `name`, `description`, `keywords`,
`llm.<action>`) ; skills.h le lit, le prompt le présente à part (« Tes outils »), et l'appel du LLM est vérifié contre lui comme
celui d'une app. La réponse de l'outil va droit dans la bulle (le LLM ne la recopie pas) et reste dans l'échange gardé, pour la
suite (« et demain ? », « imprime-la »).

| Outil | Actions | Données |
|---|---|---|
| `weather` | `now [ville]`, `day date [ville]`, `week [ville]`, `city ville` | Open-Meteo (géocodage puis prévisions, HTTPS, sans clé, CC BY 4.0) ; la ville réglée : `city` de config.ini |
| `memo` | `add texte`, `list`, `read [mots]`, `print [mots]` | les notes de Notes, `SD0:/Notes`, par `notesmodel.cpp` de Notes compilé ici tel quel |
| `planner` | `add date texte [heure] [heure]`, `list date [date]`, `print date [date]` | le `calendar.ics` du Calendrier, par son `model.h` et son `agendafiles.h` (`planner.cpp`) ; l'applet agenda du bureau refaite et prévenue comme le fait le Calendrier |
| `shopping` | `add article [quantité]`, `remove`, `check`, `uncheck`, `list`, `clear [coches]`, `print` | une note de Notes intitulée « Liste de courses » (ou « Shopping list ») : `[ ] lait (2 litres)`, `[x] pain` |

**Imprimer** : l'outil rend un titre et un texte ; la fenêtre (son fil, pas celui du cerveau) ouvre la fenêtre d'impression du
système (PrinterKit, `print_dialog`) et met le texte en pages. Rien ne part sans la personne.

**Les deux côtés voient les mêmes données** : Notes relit `SD0:/Notes` quand `notes.ini` change (chaque seconde, sauf pendant la
frappe) ; le Calendrier relit `calendar.ics` quand il change sur la carte.

**Sans LLM**, `LocalParser` comprend les phrases simples, en français et en anglais (« météo à Lyon demain », « mets 2 litres de lait
et du pain sur la liste de courses », « ajoute un rendez-vous chez le dentiste vendredi à 14h », « what do I have tomorrow? ») :
le jour (aujourd'hui, demain, un jour de la semaine, « le 12 décembre », 12/11, cette semaine, la semaine prochaine), l'heure (14h,
9h30, 14:30, midi, 3pm), les articles et leur quantité. Une question qui commence par « comment » reste une question d'aide. Les
réponses des outils suivent la langue du système (`ui_lang`).

**Pourquoi pas les skills en BASIC du PocketQI d'origine** (`system/sd/apps/<skill>/main.bas` et `galo.bi`, lancées par le shell
vocal) : Korin a bien un BASIC (`basic`), mais ces skills auraient dû tout refaire à côté des apps : lire et écrire l'iCalendar du
Calendrier (répétitions, fuseaux, l'applet agenda), le format des notes et de `notes.ini`, joindre Open-Meteo en HTTPS (le TLS de
l'OS est OpenSSL, en C seulement), et imprimer (la fenêtre d'impression vit dans le programme qui a une fenêtre). En C++ dans
PocketQI, chaque outil reprend le code même de l'app (les notes et l'agenda sont les mêmes des deux côtés, par construction), tourne
sur le fil du cerveau sans lancer de process, et marche aussi hors ligne. Ce qui est repris d'Onyx : la forme (un manifeste
`llm.<action>` par skill, les actions et leurs arguments — `meteo now/day/week`, `agenda write/read`, `courses add/remove/list` —, la
réponse dite par la skill), et Open-Meteo pour la météo.

## La voix

`voice.h`, repris de `voice.h` du PocketQI d'Onyx : la synthèse vocale de Mistral (Voxtral TTS, `POST /v1/audio/speech`, modèle
`voxtral-mini-tts-3-0`, `"stream": true`, PCM float32 24 kHz en base64 dans un flux SSE). La réponse est suivie à mesure (le
`progress` de `http_request`), portée à 44,1 kHz stéréo et jouée par AudioKit (`ak_open`, le service sound) : Pocket parle avant
que la phrase soit toute faite. Un fil à elle (`Speaker`) dit les réponses dans l'ordre ; une nouvelle question, Nouvelle
conversation ou la fermeture la coupent. Le visage parle tant que le son joue.

Quand : `voice = 1` (case « Lire les réponses à voix haute » des Réglages), ou une question qui dit « à voix haute », « à haute
voix », « aloud », « out loud » (les notes, l'agenda « me les lire »). La langue : celle du système (`ui_lang`) ; la voix :
`voice_fr`, `voice_en` (défaut Léa, Paul ; les familles fr_marie, en_paul, gb_oliver, gb_jane suivent l'humeur du visage). Le
texte est rendu prononçable (`speech_text` : cases, puces, °C, km/h, « 14:30 » -> « 14 h 30 », lignes -> pauses).

**Pourquoi Mistral et pas un moteur local** : c'est la voix du PocketQI d'Onyx, la qualité est bien meilleure que celle des
moteurs locaux, et la clé est déjà celle du service de questions. PicoTTS (le moteur local du PocketQI d'origine, Apache 2.0) a été
écarté à la demande de l'utilisateur. Sans clé Mistral ou hors ligne, il n'y a donc pas de voix.

## Réglages

Menu PocketQI > Réglages... (le bouton Réglages dans la bulle) écrit `SD0:/home/pocketqi/config.ini` :

| Clé | |
|---|---|
| `provider` | `mistral`, `groq`, `url` (un serveur compatible OpenAI) ou `local` ; vide : le premier qui a sa clé |
| `mistral_key`, `groq_key` | les clés d'API ; Groq sert de secours à Mistral, comme chez Onyx |
| `url`, `url_key` | l'adresse `.../v1/chat/completions` d'un serveur à soi, et sa clé s'il en faut une |
| `model` | facultatif : `ministral-14b-latest` (Mistral), `qwen/qwen3.8-27b` (Groq) par défaut |
| `city` | la ville de la météo quand la phrase n'en nomme pas (« Ville (météo) » dans les Réglages, ou « j'habite à ... ») |
| `weather_url` | facultatif, pour les essais : un serveur à la place d'Open-Meteo (`/v1/search`, `/v1/forecast`) |
| `voice` | `1` : les réponses dites à voix haute (la clé : `mistral_key`) |
| `voice_fr`, `voice_en` | facultatif : la voix Mistral de chaque langue (`voice.h` : `VOICES_FR`, `VOICES_EN`) |
| `tts_url` | facultatif, pour les essais : un serveur à la place de `.../v1/audio/speech` de Mistral |

**Les clés** ne vont que dans l'en-tête `Authorization` de leur service, en HTTPS (OpenSSL, certificat vérifié) ; elles ne sont
écrites ni au journal (il n'y en a pas), ni à l'écran (champs masqués), ni dans le dépôt ou un paquet. Le fichier est créé en 0600.
**Ce qui sort de l'appareil** : la question, les derniers échanges et la liste des applications installées, vers le service
choisi, avec ce que les outils ont répondu dans ces échanges. Rien d'autre (ni fichier, ni document ouvert). Pour la météo, la
ville seule part chez Open-Meteo. Avec la voix, le texte à dire part chez Mistral.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `core/pocket_visage.c`, `.h`, `core/pocket_humeur.c`, `.h` | de PocketQI par Onyx, tels quels : le visage et ses humeurs |
| `brain.h` | écrit d'après le `brain.h` d'Onyx : même boucle (outcomes, seconds tours, Mistral puis Groq, 6 échanges, 10 minutes), un prompt pour aider à se servir de l'OS au lieu de celui du robot de bureau |
| `skills.h` | écrit d'après le `skills.h` d'Onyx : le même `llm.<action> = types : description`, lu dans les `app.txt` de l'OS ; les apps sont lancées, pas des programmes BASIC |
| `http.h` | le transport de Courier (`src/apps/courier/net.h`) |
| `json.h`, `main.cpp` | écrits ; la fenêtre sur `ui_view` et les jetons du thème, sans pont |
| `config.h` | les réglages, sortis de `brain.h` |
| `voice.h` | le `voice.h` d'Onyx (Voxtral TTS, ses voix et leurs émotions), sur `http.h` et `ak_open` |
| `tools.h`, `planner.cpp`, `notesmodel.cpp`, `data/tools/*.txt` | écrits ; la forme des skills `meteo`, `agenda`, `courses` du PocketQI d'origine, le code des données de Notes et du Calendrier |

`tools/nettest/llmsrv.py` : un faux service compatible OpenAI pour les essais, qui répond d'après les manifestes reçus dans le
prompt et journalise ce qu'il reçoit (jamais la clé). Il appelle aussi les outils (une phrase sur la météo, les courses, l'agenda
ou les notes), sert de faux Open-Meteo (`weather_url = http://10.0.2.2:8088`) et de fausse voix
(`tts_url = http://10.0.2.2:8088/v1/audio/speech` : une note glissante, en SSE comme Mistral).

## Pas encore

* Le choix de la voix dans les Réglages (pour l'instant `voice_fr`, `voice_en` dans config.ini) ; la voix sans clé Mistral.
* La mémoire de PocketQI (`retenir`, les marqueurs `{nom}`), les apps BASIC, le cron, les histoires en morceaux, PocketQI Studio.
* Agir dans une app ouverte (« mets ce mot en gras ») : il faudrait que les apps exposent leurs commandes (un MCP de l'OS).
* Les outils : pas encore de suppression ni de déplacement de rendez-vous, ni d'envoi par courrier (les `genmail` d'Onyx) ; la
  fenêtre « Enregistrer » de l'imprimante PDF s'ouvre sous la bulle.
* Aucun essai contre un vrai service : seulement contre `llmsrv.py` (ni Mistral, ni le vrai Open-Meteo). Le nom des modèles par défaut vient d'Onyx (22/10/2026).
