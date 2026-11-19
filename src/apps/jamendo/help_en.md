# Jamendo

The free music of [Jamendo](https://www.jamendo.com): hundreds of thousands of tracks under Creative Commons
licences, to listen to for free, and often to download.

## The access key

Jamendo asks every application for a key: a **client_id**. Korin has its own, already in the app: nothing to do.

Its number of requests is limited and shared by everyone who uses it. If Jamendo stops answering, you can take
your own, free:

1. On [devportal.jamendo.com](https://devportal.jamendo.com), create a developer account, then an application.
2. Copy its **Client ID** (not the Client Secret, which is not used here).
3. In Jamendo: **File > Jamendo access key…**, paste it, **OK**. It is kept in your secrets vault. With the field
   emptied: back to Korin's.

## Listening

- On the left: **Popular this week**, **New releases**, and a **radio for each genre** (Rock, Jazz,
  Electronic…): the genre's tracks follow one another, more arrive when the list ends.
- At the top: **search** (tracks, artists, albums), then **Enter**.
- **Double-click** or **Enter** on a track: it plays (for a moment, it arrives).

At the bottom, the playback bar: previous, play or pause, next, the position (click it to go there), the time.

| Key | Effect |
|---|---|
| Space | play or pause |
| Ctrl+→, Ctrl+← | next, previous track |
| Ctrl+F | search |
| Ctrl+S | download the track |
| Ctrl+W | close |

## Downloading

**File > Download the track** (Ctrl+S) keeps the current track in `HOME:/music/Jamendo` ("Artist - Title.mp3"),
where the Media Player finds it. Only when its licence allows it: Jamendo says so for each track. Respect the
artist's Creative Commons licence (often: credit them, no commercial use).

## The card

Closing the window while music plays turns it into a **card** at the bottom right of the screen, above the other
windows: the music goes on. The button at the top right of the card brings the window back; closing the card stops
the music.
