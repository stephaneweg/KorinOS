# Spotify

Listen to Spotify in Korin: your liked songs, your playlists and search. The music is played by **librespot**, the
free client of Spotify's protocol. A **Premium** account is needed (a Family account is one).

## Connecting (once)

1. **File > Connect to Spotify…**
2. Jet opens Spotify's page: sign in and accept. The connection finishes by itself in the app.

No app to create on developer.spotify.com: like librespot, Korin presents itself with the identifier of Spotify's
desktop client, the only one the player accepts.

Korin keeps the connection (the oauthd service, in the secrets vault): next time, nothing to do again. **File >
Disconnect** forgets it.

## Listening

- On the left: **Liked songs**, and your **playlists**.
- At the top: **search** (tracks, artists, albums), then **Enter**.
- **Double-click** or **Enter** on a track: it plays; at the end, the next one in the list.

| Key | Effect |
|---|---|
| Space | play or pause |
| Ctrl+→, Ctrl+← | next, previous track |
| Ctrl+F | search |
| Ctrl+W | close |

## The card

Closing the window while music plays turns it into a **card** at the bottom right of the screen, above the other
windows: the music goes on. The button at the top right of the card brings the window back; closing the card stops
the music.

## Good to know

- librespot is not a Spotify product: a change on Spotify's side can stop it playing for a while, until it follows.
- **Spotify Connect**: once connected, Korin appears in the device list of the Spotify app on your phone ("Korin
  (machine name)"): pick it, the music plays here, controlled from the phone.
