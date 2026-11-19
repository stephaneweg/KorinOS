# Spotify

Écouter Spotify dans Korin : vos titres aimés, vos playlists et la recherche. La musique est jouée par **librespot**,
le client libre du protocole de Spotify. Il faut un compte **Premium** (un compte Famille l'est).

## Se connecter (une fois)

1. **Fichier > Se connecter à Spotify…**
2. Jet s'ouvre sur la page de Spotify : connectez-vous et acceptez. La connexion se termine d'elle-même dans l'app.

Pas d'app à créer sur developer.spotify.com : comme librespot, Korin se présente avec l'identifiant du client de
bureau de Spotify, le seul que le lecteur accepte.

Korin garde la connexion (le service oauthd, dans le coffre à secrets) : les fois suivantes, rien à refaire.
**Fichier > Se déconnecter** l'oublie.

## Écouter

- À gauche : **Titres aimés**, et vos **playlists**.
- En haut : la **recherche** (titres, artistes, albums), puis **Entrée**.
- **Double-clic** ou **Entrée** sur un titre : il joue ; à la fin, le suivant de la liste.

| Touche | Effet |
|---|---|
| Espace | lecture ou pause |
| Ctrl+→, Ctrl+← | titre suivant, précédent |
| Ctrl+F | la recherche |
| Ctrl+W | fermer |

## La carte

Fermer la fenêtre pendant la lecture la réduit en **carte** en bas à droite de l'écran, au-dessus des autres
fenêtres : la musique continue. Le bouton en haut à droite de la carte rend la fenêtre ; fermer la carte arrête la
musique.

## Bon à savoir

- librespot n'est pas un produit de Spotify : un changement de Spotify peut l'empêcher de jouer quelque temps, le
  temps qu'il suive.
- **Spotify Connect** : une fois connecté, Korin apparaît dans la liste des appareils de l'app Spotify de votre
  téléphone (« Korin (nom de la machine) ») : choisissez-le, la musique joue ici, pilotée depuis le téléphone.
