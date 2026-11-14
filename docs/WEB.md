# Le serveur web de Korin : webd

Le serveur web « maison » du plan du PC ([etude/PC-PILOTES.md](etude/PC-PILOTES.md), « Ordre du plan », décision du
14/11 : pas Apache, un serveur écrit pour Korin). Un seul programme, `SD0:/sys/webd` (C sur libposix, TLS par
OpenSSL), sources dans `src/sys/webd`, et son volet de Réglages, `SD0:/sys/applets/webd.so`, installés ensemble par le
paquet **webd**. WebStudio s'en servira ([DESIGN-WEBSTUDIO.md](DESIGN-WEBSTUDIO.md), §5.4).

Première version (14/11/2026) : fichiers statiques. PHP et .NET viendront par des gestionnaires (§7).

## 1. Ce qu'il sert

* **HTTP/1.1** : `GET`, `HEAD`, `OPTIONS` (les autres : 405) ; la connexion est gardée (1 000 requêtes, 15 s
  d'attente) ; un thread par connexion, 128 au plus.
* **Hôtes virtuels** : chaque site a ses liaisons (schéma, nom d'hôte, port) ; une requête va au site dont l'hôte
  correspond à `Host`, sinon au site lié à `*` sur ce port, sinon au premier site du port.
* **Fichiers** : type MIME par l'extension, `ETag` et `Last-Modified` (304), plages d'octets (206 : vidéos), page
  d'index (`index.html`, `index.htm` par défaut), redirection 301 d'un dossier sans `/`, liste d'un dossier sans index
  si `listing = 1`, `404.html` du site pour les pages absentes.
* **Refusés** (404) : `..`, `\`, `:`, et les fichiers cachés (`/.git/…`, `/.webstudio/…`), sauf `/.well-known/`
  (Let's Encrypt).
* **HTTPS** : TLS 1.2 et 1.3, un certificat par nom d'hôte choisi par **SNI** ; un hôte sans certificat en reçoit un
  **auto-signé** au démarrage (dit dans le journal). Un port sert http ou https, pas les deux.
* **Journal** : `SD0:/var/log/webd.log` (date, site, méthode, chemin, code, octets, durée) ; au-delà de 2 Mo il
  devient `webd.log.old`.

## 2. La configuration : `SD0:/etc/web/sites.ini`

```ini
[site atelier-photo]
root    = SD1:/home/sites/atelier-photo
bind    = http://*:80
bind    = https://photo.exemple.be:443
index   = index.html index.htm
listing = 0
enabled = 1
```

Une liaison : `http://` ou `https://`, un nom d'hôte ou `*` (tout nom), le port (80 et 443 par défaut). Le fichier est
réécrit par `webd site …` et par Réglages (les commentaires ne sont pas gardés, sauf l'en-tête). Les certificats sont
dans `SD0:/etc/web/certs` : `<hôte>.crt` (le certificat, puis sa chaîne) et `<hôte>.key` ; `*` : `default.crt`.

## 3. Les commandes

| Commande | Effet |
|---|---|
| `webd` | le service : les sites de la configuration ; sans site, il attend un `webd reload` |
| `webd stop`, `webd reload`, `webd status` | arrêter, relire la configuration (SIGHUP), état et sites |
| `webd check` | lit la configuration, dit ce qui manque (dossier, liaison, certificat) |
| `webd site list` | les sites |
| `webd site add NOM DOSSIER [LIAISON…]` | un site (sans liaison : `http://*:80`) |
| `webd site remove NOM`, `enable NOM`, `disable NOM`, `listing NOM 0\|1` | |
| `webd site bind NOM add\|remove LIAISON` | |
| `webd cert list` | les certificats, leur émetteur, leur fin |
| `webd cert self HÔTE [JOURS]` | un auto-signé (P-256, 825 jours) |
| `webd cert csr HÔTE` | une demande `HÔTE.csr` et sa clé en attente `HÔTE.pending.key` |
| `webd cert import HÔTE FICHIER` | le certificat reçu (et sa chaîne) pour la clé en attente ; puis `webd reload` |
| `webd cert remove HÔTE` | |
| `webd --dev DOSSIER [--port N]` | l'instance de WebStudio (§5) |

Une commande qui change la configuration fait relire le service en marche. Le numéro du service est dans
`/tmp/webd.pid`.

**Démarrer avec Korin** : la ligne `SD0:/sys/webd` dans `SD0:/etc/services.txt` (init2), que la case de Réglages
écrit ou retire. Le paquet ne l'ajoute pas : le serveur ne se lance que si l'utilisateur le demande.

## 4. Le volet de Réglages, et les applets installées

Le volet **Serveur web** n'est pas dans Réglages : c'est un module que le paquet webd pose dans
`SD0:/sys/applets/webd.so`. Réglages cherche les modules de ce dossier au démarrage et les ajoute à sa liste, après
ses volets, par ordre de nom ; sans le paquet, pas de volet (décision de l'utilisateur du 14/11 : les applets se
découvrent, comme dans Onyx, et s'affichent dans Réglages, pas dans une fenêtre à part).

* **Le contrat** (`src/apps/control/control.h`, « les applets installées ») : le module exporte en C
  `korin_applet_abi` (= `APPLET_ABI`, 1) et `korin_applet` (un `Applet` : nom, libellé, titre, icône, `open`, `apply`,
  `needs`). Il se sert du cadre de Réglages (`ap`, `ui`, `TR`, `spawn`…) et des relais d'UIKit de l'hôte.
* **Comment** : Réglages est relié en hôte de dlopen (`-rdynamic`, [POSIX.md](POSIX.md), « dlopen ») ; le module est
  `g++ -fPIC -shared -nostdlib++`, sans exceptions ni RTTI, sans libc ni libstdc++ à lui (il prend celles de
  l'hôte). Un module d'une autre ABI est laissé (message sur la sortie d'erreur de Réglages).
* **Différence avec Onyx** : là, chaque volet était un programme à part qui dessinait dans une surface partagée.
  Elegant ne compose pas la surface d'un autre programme dans une fenêtre ; le module chargé par Réglages donne le même
  résultat pour l'utilisateur, avec les widgets d'UIKit.
* Le volet : état du service (Démarrer, Arrêter), « Démarrer avec Korin », les sites (Ajouter, Activer ou
  désactiver, Retirer, Ouvrir dans Jet), les liaisons du site choisi (Ajouter, Retirer), son certificat (Auto-signé,
  Demande (CSR), Importer). Il lit et écrit `sites.ini` par le même `conf.c` que webd et passe par `webd cert` pour
  les certificats (OpenSSL reste dans webd).

## 5. L'instance de WebStudio : `webd --dev`

`webd --dev DOSSIER [--port N]` : un site sur le dossier, le premier port libre à partir de N (8080), liste des
dossiers, `Cache-Control: no-store`.

* **Sortie** : `# listen <port>` d'abord, puis une ligne par requête, séparée par des tabulations : heure, méthode,
  chemin, code, octets, ms, remarque (`fichier absent`, `rechargement`).
* **Rechargement** : chaque page HTML reçoit, avant `</body>`, un script qui écoute `/__webstudio/reload` (événements
  serveur) ; une ligne `reload` sur l'entrée de webd (ou SIGUSR1) recharge les pages ouvertes. `quit`, ou l'entrée
  fermée (WebStudio parti), arrête l'instance.
* **Limite de Korin** : netd ne lie que des ports, pas des adresses ; l'instance « 127.0.0.1 » écoute aussi le réseau
  tant que netd ne sait pas lier une adresse ([POSIX.md](POSIX.md), « Réseau »).

## 6. Vérifié

* **Sous Linux** (14/11, la même source compilée par le GCC de l'hôte, `curl`, AddressSanitizer) : index et script de
  rechargement, 206, 301, 304, 404 (absent, fichier caché, `..` encodé), 405, connexion gardée, événements de
  rechargement ; hôtes virtuels, HTTPS auto-signé et par SNI, demande de certificat signée par une autorité d'essai
  puis importée (chaîne vérifiée par `curl --cacert`), site désactivé, 50 requêtes en parallèle ; aucune erreur
  d'AddressSanitizer.
* **Korin PC sous QEMU** : §8.

## 7. Plus tard

* **Gestionnaires** PHP (FastCGI ou `php` en process) et .NET (ASP.NET Core, Kestrel derrière webd), par extension ou
  par chemin, choisis dans la configuration du site.
* **Let's Encrypt** (ACME, RFC 8555) : défi HTTP-01 servi par webd (`/.well-known/acme-challenge/`), renouvellement à
  30 jours de la fin, essais sur *staging* ; DNS-01 plus tard ([etude/PC-PILOTES.md](etude/PC-PILOTES.md)).
* Redirection http → https par liaison ; compression ; HTTP/2.
* Le Pi : la même source ; la construction AArch64 (`tools/office/build.sh`) et Réglages en hôte de dlopen restent à
  faire et à essayer.
