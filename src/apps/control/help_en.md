# Settings

**Settings** is the Korin OS control panel: the desktop theme, the keyboard and language, the screen resolution, the panel, Wi-Fi, remote access, sound, gamepads, printers... A single window: the list of panes on the left, the chosen pane on the right. Settings opens from the Korin OS menu, **System** category.

![The Settings window](help/main.png)

## The window

- **On the left**, the list of panes, each with its icon: Theme, Region, Display, Panel, Wi-Fi, Remote Access, Sound, Gamepad, Preloaded, .NET, Software, Printers. A click opens the pane. The mouse wheel scrolls the list if it is too long.
- **On the right**, the pane: its title, its settings, and at the bottom a message and the **Apply** button. Nothing changes before **Apply** (except in the panes that act at once: Wi-Fi, Preloaded, Printers).

Settings are written to files in `/etc` and kept at the next startup.

## Theme

The look of the desktop.

- **Desktop Theme**: **Givre** (light) or **Onyx Noir** (dark).
- **Accent Color**: Blue, Red Orange, Bright Green, Yellow, Purple, Fuchsia, Teal or Graphite.
- **Wallpaper**: **Cells** (the original wallpaper), **Gradient**, **Bubbles**, **Solid**, **Pattern** (‹ and › under the preview go from one pattern to the next: Bokeh, Contours, Dunes, Facets, Hexagons, Low poly, Silk, Waves; any grey picture put in `/res/wallpapers` is added) or **Image** (**Choose...** a BMP, GIF, PNG, JPEG, PCX or WebP picture; it opens `/home/public/wallpapers`, where the **Fonds d'écran** (Wallpapers) package from Software puts five wallpapers; **Tile the picture** repeats it instead of filling the screen).
- **Wallpaper tint**: none (the theme's calm greys), the theme's accent (it follows the accent), or one of the eight accent colors. A tinted picture turns into two tones of the tint.
- The **Preview**, on the right, shows a small desktop in the chosen colors, on the chosen wallpaper.

**Apply**: the wallpaper, the menu bar and the panel change at once; applications already open will take the theme when they restart.

## Region

The **Region and Language** pane: keyboard, language and time of this device.

- **Keyboard Layout**: the layouts of `/res/keyb`: **Belgian** (Belgian AZERTY), **Français** (French AZERTY), **US** (QWERTY). It changes as soon as you **Apply**. Dead keys (^ ¨ on the Belgian and French layouts, ´ ` ~ with AltGr) combine with the next letter: ^ then e gives ê; followed by a space, they give the accent alone.
- **Language**: **Français** or **English**. Each application takes it at its next start. Translation is ongoing: some applications stay in French.
- **Country (Wi-Fi)**: France, Belgium, Switzerland, Luxembourg, Canada, Germany, United Kingdom, United States. It sets the allowed Wi-Fi channels.
- **Time Zone (NTP)**: first choose a region (Europe, America, Asia...), then the city in the list below. The time comes from the network (NTP).

## Display

The screen **Resolution**, changed without restarting.

1. Choose a resolution in the list. **(native)** marks the screen's own resolution, the sharpest; **(display)** the ones the screen announces itself.
2. **Apply**: the screen changes at once.
3. A question appears: "Keep this resolution? Reverting in 15 s." Choose **Keep**, or **Revert**.

Without an answer after 15 seconds, the previous resolution comes back by itself: a black or unreadable screen cannot lock you out. A kept resolution is also used at the next startup.

## Panel

The panel is the bar at the bottom of the desktop.

- **Shortcuts**: on the left, **On the panel**, the applications that have a shortcut there; on the right, **All applications**. Choose an application on the right, then **< Add** (or double-click it). **Remove** takes the chosen shortcut away; the **^** and **v** arrows move it. Twelve shortcuts at most.
- **Desktops**: the **Count** of workspaces (from 1 to 6) and the name of each one, shown by the panel square.
- At the top of the pane: the panel **Size** (Automatic, Large, Medium, Small) and the **Trash** box.

**Apply** saves and restarts the panel; **Revert** goes back to what was saved.

## Wi-Fi

This pane acts at once, without **Apply**.

- **At the top**, the state: the network, the address, the gateway and the signal; **Disconnect** cuts the connection.
- **Visible Networks**: each network with its signal, a padlock if it is protected, a check mark on the current network. **Refresh** searches again.
- **Connect**: a click on a network unfolds its row; type its key (at least 8 characters), then **Connect**. The network is remembered.
- **Known Networks**: the remembered networks, with their **priority** (**-** and **+** buttons); **Forget** removes one.

Without a Wi-Fi chip (or under QEMU), the pane says so: "No Wi-Fi on this device".

## Remote Access

Control Korin OS from another computer.

- **Services**: four boxes. **Remote Terminal (telnet)**, **Remote Desktop (VNC)**, **Remote Windows (RDP)**, **File Sharing (FTP)**.
- **Shared Password**: type it in **Password** and **Confirm**. Empty fields: it stays the same. No space at the start or end.

**Apply** saves. Checked services start at the next startup; the password counts at once (it is read at every connection). Without a password, enabled services refuse every connection.

## Sound

- **Preferred Main Output**: **Automatic**, or a given output (USB, HDMI, headphone jack).
- **Main Volume**: the starting volume, 25 %, 50 %, 75 % or 100 %.
- **I2S Microphone (INMP441)**: starts the I2S microphone at startup (takes effect at restart).
- **Test sound when applying**: plays a short A note when you apply.

## Gamepad

The gamepad buttons seen by games and emulators.

- **Keyboard tab**: the keyboard key that stands in for each button. Touch a row, then press the key (Esc: cancel).
- **Gamepad 1, 2, 3 tabs**: what the plugged gamepad sends, live, and what applications see. **Configure** asks for each button in turn ("Press the BOTTOM button (Xbox A)"...): press it; **Esc** if your gamepad does not have this button, **Backspace** to give up. **Forget** gives the model back its default buttons.

The setting is written to `/etc/gamepad.ini` and taken at once by applications.

## Preloaded

The **Preloading** pane keeps programs in memory so that they start faster.

- On the left, **Programs**: the applications and programs of the card.
- On the right, **Preloaded**: those in the list, with their state (**preloaded** or **not yet**).
- The **→** arrow adds the program chosen on the left and loads it at once; the **←** arrow removes the one chosen on the right. **Refresh** reads both lists again.

## Startup

The **Startup** pane shows what Korin starts and how, like the Windows Services console. Each line gives the program's name, what it does, its state (**Running** or **Stopped**, read again every second) and its **startup type**. Choose a line (click it, or use the **Up** and **Down** arrows): the bar at the top acts on it.

- **Start**, **Stop**, **Restart**: right away, without waiting for the next startup.
- **Automatic**: started at every startup.
- **Manual**: not started at startup, but when a service that needs it starts, or with **Start**.
- **Disabled**: never started, neither by another service nor with **Start**.
- Under the bar, **Needs** and **Needed by** give the dependencies of the chosen service.

A changed type shows in color; **Apply** saves it, taking effect at the **next startup** (or the next session).

The three parts:

- **System (always started)**: files, the event bus, hardware, input, the Elegant desktop. They are essential: no action.
- **System services**: the services of `/etc/services.txt` (network, sound, remote access, updates…), started for the whole machine. Starting, stopping them or changing their type asks for your password (elevation).
- **Session programs**: those of the desktop (`/etc/session`, or `/etc/session.poche` in pocket mode: wallpaper, menu bar, panel, notifications…) and those of your `startup.txt`. This choice is **yours only**: it does not change other accounts. For them, **Manual** means "only with **Start**".

Programs marked **Essential** (pseudo-terminals, PS/2 keyboard and mouse, menu bar, panel…) stay automatic and cannot be stopped; the menu bar and the panel can be restarted. Others (network, sound, updates, graphics processor, notifications, clipboard) ask for confirmation, as does stopping a service that others use.

Types are written in `/etc/services.off` and `/etc/services.manual` (services), `HOME:/etc/session.off` and `HOME:/etc/session.manual` (the session), one line per program; dependencies are in `/etc/services.deps`. `/etc/services.txt` and `/etc/session` do not change: system updates can still replace them. A line commented out by another pane (for example **Remote access**) shows as disabled; another type removes the `#`.

## .NET

The **.NET Assemblies** pane: one box per `.dll` library of `/devtools/dotnet`, with its size. Checked, they are kept in memory and shared by .NET programs, which start faster. **Apply** loads them, and will do it again at each startup. If .NET is not on the card, the pane says so.

## Software

Updates and packages have their own window: choosing this pane opens [Software](help:pkgman), and the **Open** button opens it again.

## Printers

This pane acts at once.

- **Printers**: the PDF printer, always there (it asks for the file name at each print), and the added network printers. For the chosen one: **Default** makes it the default printer, **Test** prints a test page, **Check** asks the printer for its state, **Remove** removes it (not the PDF printer).
- **Add a Network Printer**: **Search** scans the network; pick one, then **Add**. Or type its **Address** (its IP address, like 192.168.1.30, or `ipp://host:631/ipp/print`) and an optional **Name**, then **Add**. IPP Everywhere and AirPrint printers work without a driver.
- **Print Queue**: the jobs and their state (waiting, preparing, sending, printing, done, failed, canceled). **Cancel** stops the chosen job; **Clear Finished** cleans up the list.

The PDF printer's test page is saved as `HOME:/documents/Page d'essai.pdf`.

## Opening a pane directly

The name of a pane, given as an argument, opens Settings on that pane, for example from the [Terminal](help:terminal):

```sh
control wifi
```

| Pane | Name |
|---|---|
| Theme | `theme` |
| Region | `clavier` |
| Display | `ecran` |
| Panel | `panelconf` |
| Wi-Fi | `wifi` |
| Remote Access | `remote` |
| Sound | `sound` |
| Gamepad | `padconf` |
| Preloaded | `preload` |
| Startup | `startup` |
| .NET | `dotnet` |
| Software | `pkgman` |
| Printers | `printconf` |

## Tips

- After changing the theme or the language, close and reopen the open applications so that they take it.
- If a resolution makes the screen unreadable, touch nothing: it cancels itself after 15 seconds.
- The [PocketQI](help:pocketqi) assistant can open Settings on the right pane: "change the theme".
