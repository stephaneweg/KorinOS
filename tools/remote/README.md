# Korin Remote

Le client Windows du bureau à distance de Korin OS (`rdpd`, port 3390,
`src/svc/rdpd/rdpd.asm`) : chaque fenêtre du système devient une fenêtre du PC,
avec son contenu et, au choix, son cadre ; le clavier et la souris repartent vers
le Pi. Le bouton « Terminal » ouvre une console telnet (`telnetd`, port 23).

## Mot de passe

`rdpd` demande le mot de passe de l'accès à distance (`SD0:/etc/remote.txt`,
choisi au premier démarrage ou dans Réglages, Accès à distance) : le serveur
envoie un défi de 16 octets, le client répond HMAC-SHA256 (clé : le mot de
passe en UTF-8). Le mot de passe ne passe jamais sur le réseau. « Remember » le
garde dans `%APPDATA%\KorinRemote.txt`, chiffré pour le compte Windows (DPAPI).

`rdp_auth_test.py` essaie ce protocole sans le client :
`python -I tools/remote/rdp_auth_test.py HOTE[:PORT] MOT_DE_PASSE`.

## Construction

.NET Framework 4.8 (présent sur Windows 10 et 11), avec le SDK .NET :

    cd tools/remote
    dotnet build -c Release

Le programme : `tools/remote/bin/Release/KorinRemote.exe`.

## Origine

Copie d'Onyx Remote (`pc/OnyxRemote` du projet Onyx, même auteur, licence MIT),
renommée et complétée du mot de passe. Les noms du protocole (`ONYXRDP1`,
`ONYXRDP2`) restent ceux d'Onyx ; Korin Remote parle encore sans mot de passe
au `rdpd` d'Onyx.
