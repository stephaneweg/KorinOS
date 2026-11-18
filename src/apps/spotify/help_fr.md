# Spotify

Écouter Spotify dans Korin : vos titres aimés, vos playlists et la recherche. La musique est jouée par **librespot**,
le client libre du protocole de Spotify. Il faut un compte **Premium** (un compte Famille l'est).

## Se connecter (une fois)

Spotify demande que chaque programme ait son identifiant. Le vôtre, gratuit :

1. Sur [developer.spotify.com](https://developer.spotify.com/dashboard), connectez-vous, **Create app**.
2. Dans **Redirect URIs**, ajoutez exactement `http://127.0.0.1:8898/` ; cochez **Web API** et **Web Playback
   SDK** ; enregistrez.
3. Copiez le **Client ID** de l'app.
4. Dans Spotify (Korin) : **Fichier > Se connecter à Spotify…**, collez le Client ID, **OK**.
5. Jet s'ouvre sur la page de Spotify : acceptez. La connexion se termine d'elle-même dans l'app.

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
- Pas encore : piloter Korin depuis l'app Spotify du téléphone (Spotify Connect).
