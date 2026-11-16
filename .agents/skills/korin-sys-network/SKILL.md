---
name: korin-sys-network
description: Le réseau de Korin OS en ligne de commande — état et connexion Wi-Fi, adresses et sockets, ping, DNS, transferts (ftp, wget, git en HTTPS), serveurs telnet, ftp et http de la machine. À charger pour diagnostiquer ou configurer le réseau, ou pour télécharger quelque chose.
---

# Réseau

La pile est le service `netd`, avec un client DHCP et un client NTP. Elle ne connaît que l'**IPv4**. Le Wi-Fi est le
service `wifi`. Programmer le réseau en C : `korin-kit-netkit` et `korin-posix`. Référence : `docs/RESEAU.md`,
`docs/WIFI.md`.

## Wi-Fi

| Commande | Effet |
|---|---|
| `wifi` | état de la connexion |
| `wifi scan` | réseaux visibles |
| `wifi join SSID [clé]` | se connecter, et retenir le réseau (sans clé : réseau ouvert) |
| `wifi leave` | se déconnecter (plus de reconnexion automatique) |
| `wifi known` / `wifi forget SSID` | réseaux connus (`/etc/wifi.txt`, clés non affichées) / en oublier un |
| `wifi priority SSID N` | priorité (le plus grand d'abord) |
| `wifi country XX` | pays de la réglementation radio |

**La clé Wi-Fi est un secret** : demandez-la à l'utilisateur et ne la recopiez pas dans vos réponses.

## Diagnostic

| Commande | Effet |
|---|---|
| `ping` | essai complet : la passerelle, puis un nom résolu par DNS |
| `ping HÔTE [N]`, `ping -t HÔTE` | N pings (4 par défaut), ou jusqu'à Ctrl+C |
| `nslookup NOM` | l'adresse d'un nom, par le DNS du bail DHCP (pas de résolution inverse) |
| `netstat [-t] [-u] [-l] [-i]` | interfaces et configuration IP (`-i`), sockets TCP/UDP, en écoute (`-l`), avec le programme |
| `bus get korin.net.state` | état du réseau publié sur le bus |

## Transferts

- **HTTPS :** `git clone https://…` marche (git a libcurl et OpenSSL). Il n'y a pas de commande `curl` ;
  `wget URL` et `httpget` ne savent que le HTTP.
- **Un téléchargement HTTPS** passe par git ou par un petit programme qui utilise OpenSSL (`korin-lang-c`).
- **`ftp`** : client FTP.

## Serveurs de la machine

- `telnetd` écoute sur le port 23, **sans mot de passe**, et ouvre bash ; `ftpd` sert les fichiers. Les deux sont
  dans `/etc/services.txt`.
- `httpd` sert des pages.
- **Rien de tout cela n'est protégé** : ne les ouvrez qu'au réseau local, et ne les signalez jamais comme sûrs.

## Pièges

- Pas d'IPv6, et pas de socket `AF_UNIX` nommé.
- `connect` bloque jusqu'à la fin de la poignée de main.
- `bind` sur le port 0 ne choisit pas de port : un serveur doit prendre un port fixe.
- L'heure vient du NTP au premier bail DHCP. Sans réseau, l'horloge peut être fausse : un certificat TLS peut alors
  sembler invalide.
