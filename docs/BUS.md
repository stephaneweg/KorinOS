# Le bus d'événements : busd et BusKit

Les programmes se préviennent par **sujets nommés**, sans se connaître : un programme publie, ceux
qui se sont abonnés reçoivent un événement. C'est la part « signaux » de D-Bus, des notifications
distribuées de macOS ou de PPS dans QNX. Les appels directs entre programmes restent les services
nommés du noyau (`SVC_OPEN` par le nom, puis `SVC_CALL`) : le bus ne se met pas sur leur chemin.

| Pièce | Où | Rôle |
|---|---|---|
| `busd` | `src/svc/busd`, `SD0:/sys/svc/busd` | le service `bus` ; lancé par init2 en premier, relancé s'il tombe |
| BusKit | `src/kits/buskit`, `SD0:/lib/buskit.so` | les fonctions `bus_*` des programmes (C : `buskit_abi.h`) |
| `bus` | `src/bin/bus`, `SD0:/bin/bus` | le bus en ligne de commande, pour essayer et regarder |
| protocole | `src/inc/bus.inc` | les ops du service ; constantes publiques : `buskit.inc` |

## Sujets et valeurs

* Un sujet est un nom de 63 octets au plus, sans espace ni `*`. Convention : des mots séparés par
  des points, du général au particulier, `korin.` pour le système (`korin.theme`,
  `korin.usb.device`, `korin.net.state`), le nom de l'app pour le reste (`arabesque.preset`).
* Une valeur fait 256 octets au plus : du texte, un nombre, une petite structure. Les gros contenus
  passent par un fichier ou un tampon partagé, et le sujet dit où.
* La dernière valeur publiée reste lisible (`bus_get`). Publiée avec `BUS_RETAIN`, elle est aussi
  **l'état courant** : un abonné `BUS_CURRENT` la reçoit dès son abonnement. Une app lancée après
  le branchement d'une manette apprend donc qu'elle est là, sans rien avoir manqué.
* `BUS_DELETE` retire un sujet ; ses abonnés reçoivent un événement de longueur `BUS_DELETED`.
* 128 sujets et 256 abonnements dans tout le système (`busd.asm` : `TOPIC_MAX`, `SUB_MAX`).

## Abonnements et événements

* Un motif est un nom exact (`korin.theme`) ou un préfixe suivi de `*` (`korin.usb.*`, `*` : tout).
* Chaque publication dépose `WEV_BUS` (13) dans la **file d'événements du programme**, la même que
  celle des fenêtres : `ui_event` le rend comme les autres, et un programme sans fenêtre l'attend
  avec `bus_wait`. Un seul endroit où attendre, sans thread de plus.

| Décalage | Taille | Contenu |
|---|---|---|
| +0 | 32 bits | `WEV_BUS` |
| +4 `BEV_SUB` | 32 bits | l'abonnement (ce que `bus_subscribe` a rendu) |
| +8 `BEV_TOPIC` | 32 bits | le sujet : `bus_get_id`, `bus_name` |
| +12 `BEV_LEN` | 32 bits | octets de la valeur ; `BUS_DELETED` : sujet retiré |
| +16 `BEV_DATA` | 12 octets | le début de la valeur (zéros après) : une valeur courte n'a pas besoin de `bus_get` |
| +28 | 32 bits | 0 (pas de fenêtre) |

* Un programme abonné plusieurs fois à un même sujet (deux motifs) reçoit un événement par
  abonnement.
* File pleine (64 événements) : l'événement est perdu pour ce programme, la valeur reste lisible.
* Fin d'un programme : busd est prévenu (`SVF_FINS`) et retire ses abonnements.
* busd relancé : BusKit rouvre le canal tout seul, mais les sujets et les abonnements sont perdus
  (à refaire ; les éditeurs republient leur état).

## BusKit

```
bus_available ()                        -> 1 busd répond
bus_publish (nom, valeur, octets, drapeaux)  -> abonnés prévenus   (octets -1 : chaîne)
bus_subscribe (motif, BUS_CURRENT)      -> abonnement (> 0)
bus_unsubscribe (abonnement)            -> 0                  (0 : tous ceux du programme)
bus_get (nom, tampon, taille)           -> octets de la valeur
bus_get_id (sujet, tampon, taille)      -> octets
bus_name (sujet, tampon 64)             -> 0
bus_list (sujet, tampon 64)             -> sujet suivant (0 : fin)
bus_wait (événement 32 o, attente ms)   -> type, sans UIKit
```

Les erreurs sont des `BUS_E*` négatifs : `BUS_ENODEV` (busd absent), `BUS_EINVAL` (nom, motif,
valeur), `BUS_ENOENT`, `BUS_ENOMEM`.

```asm
	adr	x0,s_theme			; s_theme db 'korin.theme',0
	mov	x1,BUS_CURRENT
	bl	bus_subscribe
	...
	bl	ui_event
	cmp	x0,WEV_BUS
	b.eq	.theme				; ui_ev+BEV_DATA : le nom du thème
```

## En ligne de commande

```
bus list                     les sujets et leur valeur
bus get korin.theme          une valeur
bus pub -r korin.theme Givre publie (-r : gardée)
bus del korin.theme          retire
bus watch -c korin.*         les événements jusqu'à Ctrl+C (-c : d'abord l'état courant)
```

## Dans le noyau

Une seule chose : un service enregistré avec **`SVF_POST`** (8) peut appeler `WS_POST`, qui ne
servait qu'au serveur graphique (`ws.asm`, `svc_may_post` dans `svc.asm`). Seuls les programmes de
`SD0:/sys` s'enregistrent. busd est le seul à le demander.

## La suite

* Les premiers éditeurs : le thème (`korin.theme`, publié par qui l'applique), l'USB (usbd et ses
  pilotes : `korin.usb.device`, `korin.input.pad`, `korin.storage.volume`), le réseau
  (`korin.net.state`), le son (`korin.audio.output`).
* Les appels de méthode par le bus (le reste de D-Bus, COM, XPC) : un nom de service, des méthodes,
  l'activation à la demande. Les services nommés du noyau en font déjà l'essentiel.
* Les droits : aujourd'hui tout programme peut publier sous tout nom ; `korin.*` pourrait être
  réservé aux programmes de `SD0:/sys`.
