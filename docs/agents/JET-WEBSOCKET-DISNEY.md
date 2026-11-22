# Jet : le WebSocket de Disney+ échoue (30 s de chargement perdues) — à corriger dans WebKit

Mesuré le 22/11/2026 sur le ZBook (Korin PC, Jet 1.4.x, app `disneyplus`) : une sonde JavaScript
(`/etc/web-probe.js` + `/etc/web-console`), des poignées de main refaites en Python, et un serveur WebSocket local
qui répond de plusieurs façons à Jet. Relevés bruts sur la machine : `~/jet-mesures/3/` (sonde dans Disney+) et
`~/jet-mesures/4/` (serveur local) — pas dans le dépôt.

## En une phrase

**Le client WebSocket de Jet (port curl de WebKit) refuse toute réponse qui contient un en-tête
`Sec-WebSocket-Extensions`** — même `x-webkit-deflate-frame`, l'extension qu'il demande lui-même — avec
« *Sec-WebSocket-Extensions header is invalid* ». Le serveur de Disney accepte l'extension et la renvoie : la
connexion échoue, et le SDK de Disney attend 2 × 15 s avant de s'en passer.

## Le symptôme

Disney+ met **≈ 57 s** à afficher ses vignettes dans Jet (≈ 30 s en partant directement de
`https://www.disneyplus.com/fr-fr/home`). Pendant les attentes : ni réseau, ni calcul, ni dessin (machine à 2-5 %).

## Ce que fait le site

1. Au démarrage, le SDK de Disney (`js-client-sdk-*.js`, BAMTech/DSS 35.9.2) ouvre :

   ```
   new WebSocket("wss://disney.connections.edge.bamgrid.com/04fe8d/connection?X-Application-Version=8b3d3644_bap
     &X-BAMSDK-Client-ID=disney-svod-3d9324fc&X-BAMSDK-Platform=javascript/linux/chrome&X-BAMSDK-Version=35.9.2
     &X-DSS-Edge-Accept=vnd.dss.edge+json; version=2&X-Request-Yp-Id=6930997327b53ccd97512ce3&X-Request-ID=<uuid>",
     ["vnd.dss.edge+json"])
   ```

   (aucun jeton dans l'adresse : l'authentification passe par un message après l'ouverture.)
2. Dans Jet : `error` puis `close` **1006** (`wasClean=false`) après 150-220 ms, jamais d'`open`. La console de
   WebKit (`/etc/web-console`) dit : `WebSocket connection to 'wss://disney.connections.edge.bamgrid.com/04fe8d/connection?…'
   failed: Sec-WebSocket-Extensions header is invalid`.
3. Pendant son démarrage, le SDK ne se rabat pas sur une fermeture (`socketManagerInitCallback` :
   `this.socketManager.isStartPending || await this.fallback(P)`) : il attend `userStartAttemptTimeout` (**15 s**,
   « SDK Sockets timed out waiting for connection to authenticate »), réessaie une fois (`await y() || await y()`),
   encore 15 s, puis se rabat : **30 s**. Le site démarre aussitôt après.
4. Avec l'adresse de départ de l'app (`https://www.disneyplus.com/`), la page fait ensuite une vraie navigation vers
   `/fr-fr/home` : tout recommence, encore 30 s.

## La requête de Jet et la réponse de Disney

La poignée de main que Jet envoie (relevée par le serveur local) :

```
GET /ws/... HTTP/1.1
Upgrade: websocket
Connection: Upgrade
Host: ...
Origin: ...
Sec-WebSocket-Protocol: vnd.dss.edge+json
Pragma: no-cache
Cache-Control: no-cache
Sec-WebSocket-Key: ...
Sec-WebSocket-Version: 13
Sec-WebSocket-Extensions: x-webkit-deflate-frame
User-Agent: ...
Sec-Fetch-Dest: websocket
Sec-Fetch-Mode: websocket
Sec-Fetch-Site: same-origin
```

La même, rejouée en Python vers le vrai serveur (CloudFront, TLS 1.3, HTTP/1.1) : `101 Switching Protocols`, et le
serveur **accepte l'extension** :

| `Sec-WebSocket-Extensions` demandé | réponse de Disney |
|---|---|
| `x-webkit-deflate-frame` (Jet) | `sec-websocket-extensions: x-webkit-deflate-frame` |
| `permessage-deflate; client_max_window_bits` (Chrome) | `sec-websocket-extensions: permessage-deflate` |
| aucun | pas d'en-tête d'extensions |

`wss://echo.websocket.org/` marche dans Jet parce qu'il ne renvoie **aucune** extension.

## L'essai avec un serveur local (reproduit sans Disney)

Une page servie en `http://127.0.0.1:<port>/`, ouverte par `/apps/jet.app/main http://127.0.0.1:<port>/`, ouvre un
WebSocket vers chaque variante ; le serveur (Python, sockets brutes) répond `101` avec les en-têtes de la variante,
puis répond `pong` au `ping` du client.

| Variante (en-têtes de la réponse `101`) | Jet |
|---|---|
| `upgrade`, `connection: Upgrade`, `sec-websocket-accept` (comme l'écho, fly.io) | ouvert, `pong` reçu |
| `Connection: upgrade` (valeur en minuscules, ordre de CloudFront) | ouvert |
| tout en minuscules | ouvert |
| + `sec-websocket-protocol: vnd.dss.edge+json` (protocole demandé) | ouvert, `protocol` = `vnd.dss.edge+json` |
| idem en `Sec-WebSocket-Protocol` capitalisé | ouvert |
| protocole demandé, pas renvoyé | ouvert, `protocol` = `""` (Chrome refuserait ; sans importance ici) |
| la réponse exacte de CloudFront **sans** extensions (`X-Cache`, `Via`, `X-Amz-*`…) | ouvert |
| adresse avec la requête de Disney (espace, `+`, `;` : Jet envoie `;%20version=2`) | ouvert |
| + `sec-websocket-extensions: x-webkit-deflate-frame` | **refusé** : « Sec-WebSocket-Extensions header is invalid », 1006 |
| idem en `Sec-WebSocket-Extensions` capitalisé | **refusé**, même message |
| + `sec-websocket-extensions: permessage-deflate` | **refusé**, même message |
| la réponse de Disney avec `x-webkit-deflate-frame` | **refusé**, même message |
| le vrai `wss://disney.connections.edge.bamgrid.com/…` avec sa requête | **refusé**, même message |
| le vrai serveur sans la requête | `Unexpected response code: 400` (normal : les paramètres sont nécessaires) |

Dans les cas refusés, Jet ferme la connexion juste après la réponse, sans envoyer de trame. Même message pour
l'extension demandée (`x-webkit-deflate-frame`) et pour une extension non demandée (`permessage-deflate`) : ce
n'est apparemment pas un refus d'extension inconnue (qui a normalement son propre message dans
`WebSocketExtensionDispatcher`, à vérifier), **l'analyse de l'en-tête semble échouer toujours**.

Serveur minimal pour reproduire (la variante qui échoue) :

```python
import base64, hashlib, socket
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", 8770)); s.listen(4)
PAGE = b"<script>var w=new WebSocket('ws://'+location.host+'/ws');w.onopen=function(){console.log('OUVERT')};w.onclose=function(e){console.log('FERME '+e.code)}</script>"
while True:
    c, _ = s.accept(); req = c.recv(4096).decode("latin-1")
    if "Upgrade: websocket" not in req:
        c.send(b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n" % len(PAGE) + PAGE); c.close(); continue
    key = [l.split(":", 1)[1].strip() for l in req.split("\r\n") if l.lower().startswith("sec-websocket-key:")][0]
    acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    c.send(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
            + acc + "\r\nSec-WebSocket-Extensions: x-webkit-deflate-frame\r\n\r\n").encode())   # sans cette ligne : OUVERT
```

## Où chercher dans WebKit

- Le message vient de `WebSocketExtensionDispatcher::processHeaderValue`
  (`Source/WebCore/Modules/websockets/WebSocketExtensionDispatcher.cpp`) : il le dit quand
  `WebSocketExtensionParser` (`WebSocketExtensionParser.cpp`) n'arrive pas à lire l'en-tête. Or
  `x-webkit-deflate-frame` est un jeton valide : **la valeur qui arrive au parseur n'est pas celle du serveur**. À
  vérifier en premier : ce que le client du port curl (`Source/WebKit/NetworkProcess/curl/WebSocketTaskCurl.cpp`,
  `Source/WebCore/platform/network/curl/` : `CurlStream*`, l'analyse de la réponse de la poignée de main) met dans
  les en-têtes de la réponse — retour chariot ou espace laissés en fin de valeur, valeur vide, en-tête lu deux
  fois ou concaténé (`"x, x"`), mauvaise longueur, chaîne 8/16 bits. `Sec-WebSocket-Protocol` passe, lui (comparé
  autrement).
- `WebSocketHandshake::readServerHandshake` / `checkResponseHeaders`
  (`Source/WebCore/Modules/websockets/WebSocketHandshake.cpp`) : par où passe la valeur de l'en-tête jusqu'au
  dispatcher.
- Vérifier aussi que le processeur de `x-webkit-deflate-frame` (`WebSocketDeflateFramer`, `USE(ZLIB)`) est bien
  construit dans le port : s'il ne l'est pas, Jet ne devrait pas proposer l'extension.

## Les correctifs possibles

1. **Corriger la lecture de l'en-tête** pour que `x-webkit-deflate-frame` accepté par le serveur marche (la
   compression des trames en plus). C'est le bon correctif si le défaut est dans le port curl.
2. **À défaut, ne plus proposer d'extension** (pas de `Sec-WebSocket-Extensions` dans la requête) : les serveurs
   n'en renvoient alors aucune ; vérifié en Python sur le serveur de Disney (101, pas d'en-tête d'extensions).
   Les WebSockets marchent sans compression.

Un patch de plus dans `tools/webkit/patches/` (noté dans le `.armpi-patches` de chaque arbre, `webkit` et
`webkit-gl`), Jet reconstruit. Vérifier d'abord sous QEMU avec le serveur minimal ci-dessus (`OUVERT`), puis sur le
ZBook : `/apps/disneyplus.app/main` doit afficher les vignettes en quelques secondes au lieu de ≈ 57 s. Ne
publier jet qu'avec l'accord de l'utilisateur. Le même défaut touche tout site dont le serveur WebSocket accepte la
compression (beaucoup de serveurs `permessage-deflate` ; `x-webkit-deflate-frame` est plus rare).

En attendant (sans reconstruire) : `url = https://www.disneyplus.com/fr-fr/home` dans
`src/apps/disneyplus/webapp.txt` gagne ≈ 25 s (une seule attente), mais impose le français ; pas fait.

## Écartés en chemin

- Le cadre `login.disney.com` (OneID) : `getData` / `getDataResponse` aboutissent en 2 s.
- Le Worker de Datadog RUM (minuterie de 30 s, « failed to start: a timeout occurred while initializing the
  Worker ») : il répond `initialized` en 140 ms ; sa minuterie expire sans rien faire.
- Les Workers créés d'un blob : 15-90 ms dans chaque cadre.
- La casse et l'ordre des en-têtes de la réponse, `Connection: upgrade`, le protocole renvoyé, l'adresse avec espace
  et `+` : tous acceptés (essai local).
- L'ALPN (le serveur de Disney et l'écho choisissent tous deux `h2` si on le propose, l'écho marche), le certificat
  (Amazon RSA 2048 M04, le même que `disney.api.edge.bamgrid.com` que Jet joint en HTTPS), une CSP (la page n'en a
  pas), un message du serveur juste après le `101` (il n'envoie rien).
- Le réseau HTTP (≈ 300 requêtes rapides), la composition GPU (lente, mais pas la cause de l'attente), les cookies
  tiers (enregistrés), l'en-tête User-Agent.
