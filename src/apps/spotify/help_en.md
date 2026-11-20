# Spotify

Listen to Spotify in Korin: **Spotify's web player** (open.spotify.com) in its own window, with its icon, on Jet's
engine. Everything the web player does is there: home, search, library, playlists, podcasts. Nothing to register at
Spotify: no Client ID, it is Spotify's own web client, signed in with your account.

Korin PC only: Spotify protects its music with **Widevine**, Google's module that Jet can run on the PC.

## The first time

1. **The Widevine module**: if it is not there yet, Spotify offers to download it from Google (about 22 MB, checksum
   verified) and to install it in your home folder. It is the same one as Jet's (Settings, "Play protected
   content"): one download for both.
2. **Your account**: sign in in the window, as on Spotify's site. The app keeps its own session (its cookies are its
   own, apart from Jet's): next time, nothing to do again.

Sign-ins that open another window (some "Continue with…" buttons) open in Jet, which does not have the app's
session: prefer your e-mail address and password, or the code Spotify sends.

## Listening

As on the site: click a track, a playlist, an album. The **Playback** menu and the card drive the page's playing
bar.

| Key | Effect |
|---|---|
| Space | play or pause (Spotify's shortcut) |
| Ctrl+R, F5 | reload the page |
| Alt+← | previous page |
| Ctrl+W | shrink to the card while playing, otherwise close |
| Ctrl+Q | quit |

## The card

Closing the window while playing shrinks it to a **card** at the bottom right of the screen, above the other
windows: the music goes on. The card shows the title, the artist and the position; its buttons: previous, play or
pause, next. The button at its top right brings the window back; closing the card stops the music and the app.
**Playback > Shrink to a card** shows it at any time.

## The File menu

- **Reload**, **Back**: the page.
- **Open in Jet**: Spotify in the browser (with Jet's session).
- **Sign out (clear the data)…**: clears the app's session (cookies, storage); Jet's does not change.

## Good to know

- On protected sites, the engine presents itself as Chrome on Linux: Spotify treats it as an ordinary browser (the
  web player's sound quality).
- Without a Premium account, Spotify's web player plays with ads, as elsewhere.
