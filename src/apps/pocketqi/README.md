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

## Réglages

Menu PocketQI > Réglages... (le bouton Réglages dans la bulle) écrit `SD0:/home/pocketqi/config.ini` :

| Clé | |
|---|---|
| `provider` | `mistral`, `groq`, `url` (un serveur compatible OpenAI) ou `local` ; vide : le premier qui a sa clé |
| `mistral_key`, `groq_key` | les clés d'API ; Groq sert de secours à Mistral, comme chez Onyx |
| `url`, `url_key` | l'adresse `.../v1/chat/completions` d'un serveur à soi, et sa clé s'il en faut une |
| `model` | facultatif : `ministral-14b-latest` (Mistral), `qwen/qwen3.8-27b` (Groq) par défaut |

**Les clés** ne vont que dans l'en-tête `Authorization` de leur service, en HTTPS (OpenSSL, certificat vérifié) ; elles ne sont
écrites ni au journal (il n'y en a pas), ni à l'écran (champs masqués), ni dans le dépôt ou un paquet. Le fichier est créé en 0600.
**Ce qui sort de l'appareil** : la question, les derniers échanges et la liste des applications installées, vers le service
choisi. Rien d'autre (ni fichier, ni document ouvert).

## Repris et écrit

| Fichier | Origine |
|---|---|
| `core/pocket_visage.c`, `.h`, `core/pocket_humeur.c`, `.h` | de PocketQI par Onyx, tels quels : le visage et ses humeurs |
| `brain.h` | écrit d'après le `brain.h` d'Onyx : même boucle (outcomes, seconds tours, Mistral puis Groq, 6 échanges, 10 minutes), un prompt pour aider à se servir de l'OS au lieu de celui du robot de bureau |
| `skills.h` | écrit d'après le `skills.h` d'Onyx : le même `llm.<action> = types : description`, lu dans les `app.txt` de l'OS ; les apps sont lancées, pas des programmes BASIC |
| `http.h` | le transport de Courier (`src/apps/courier/net.h`) |
| `json.h`, `main.cpp` | écrits ; la fenêtre sur `ui_view` et les jetons du thème, sans pont |

`tools/nettest/llmsrv.py` : un faux service compatible OpenAI pour les essais, qui répond d'après les manifestes reçus dans le
prompt et journalise ce qu'il reçoit (jamais la clé).

## Pas encore

* La voix (Mistral, Voxtral) : PocketQI parle d'Onyx à voix haute ; ici, seulement la bulle.
* La mémoire de PocketQI (`retenir`, les marqueurs `{nom}`), les apps BASIC, le cron, les histoires en morceaux, PocketQI Studio.
* Agir dans une app ouverte (« mets ce mot en gras ») : il faudrait que les apps exposent leurs commandes (un MCP de l'OS).
* Aucun essai contre un vrai service : seulement contre `llmsrv.py`. Le nom des modèles par défaut vient d'Onyx (22/10/2026).
