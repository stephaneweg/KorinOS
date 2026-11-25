# Files

**Files** is the Korin OS file browser. On the left, the connected volumes (the SD card, a USB drive...) and the quick access places; on the right, the content of the current folder. Use it to browse the card, open a document or an application, create, copy, move, rename and delete files and folders, and unmount a USB drive before removing it. Files opens from the Korin OS menu, **System** category.

![The Files window](help/main.png)

## The window

- **The left panel** first shows the **Quick Access** places (Home, Music, Pictures, Downloads, Projects, Applications, Documents, Videos...), each with its icon, then the **Volumes**: by default only USB drives and network volumes (shares, Google Drive); **View > All Volumes** adds **/**, named **System** (the other volumes, **/tmp**..., are in its tree). A share is called "share on server", a Google Drive "Google Drive". Each volume shows its name, its free space and its size ("12.3 GB free of 29.7 GB") with a gauge of the space used (red above 90%); a volume whose space is not known (some shares) shows its kind: **SD Card**, **USB Drive**, **RAM**, **Server**... A USB drive has an eject button at the right of its row. The volume list updates by itself when a drive is plugged in or removed.
- **The header**, at the top right: the ← (back), → (forward) and ↑ (parent folder) buttons, the **breadcrumb** (the path of the current folder, one segment per folder), and the view choice: icons or list.
- **The content** of the folder: folders first, then files, in alphabetical order. An application (a `.app` folder) shows with its icon and its name.
- **The status line**, at the bottom: the number of items, or the name and size of the chosen item, or the result of the last action ("Deleted", "Volume unmounted"...); on the right, the free space of the current volume and its gauge.

## Moving around

- **Enter a folder**: double-click it, or choose it and press Enter.
- **Go up one folder**: the ↑ button, the Backspace key, or **Go > Parent Folder**. The folder you just left stays chosen.
- **Go back, then forward**: the ← and → buttons. They are grayed out when there is nothing before or after.
- **Jump to a folder of the path**: click a segment of the breadcrumb. A path that is too long is cut on the left; the "…" leads to the folder it replaces.
- **Go to a quick access place or a volume**: click it in the left panel, or use the **Go** menu, which lists the quick access places and **/**.
- **Find a recently opened file**: the **Recent** quick access lists the last files opened (by Files, by "Open With…" or by the Open or Save dialog of an application), the most recent first, 50 at most. Right-click: **Open Item Location** (its folder, with it chosen) or **Remove from Recent** (also the Delete key): the file itself stays. The list is `HOME:/etc/recent-files.txt`.
- **Type a path**: Ctrl+L (**Go > Go to Location…**), a click in the breadcrumb next to the names, or the **/** or **~** key in the view: the path becomes a field. Type `/apps`, `~/documents` (`~`: the home folder), `HOME:/music` or a name relative to the current folder (`..` goes up). **Tab** completes the last name, **Enter** goes there (a file: its folder opens, with it chosen), **Escape** or a click elsewhere closes the field.
- **Changes made by other programs**: what another program creates, deletes or renames in the shown folder (the Terminal, a save, a download) appears by itself, without Refresh.
- **Jump to an item by typing its name**: in the view, type the beginning of the name, the item that starts that way is chosen (the status line shows what is typed). Letters typed less than a second apart add up; the same letter repeated goes to the next item that starts with it.

## Opening a file

Double-click, or Enter (menu **File > Open**). What happens depends on the item:

| Item | What opens |
|---|---|
| A folder | you enter it |
| An application (`name.app`) | the application starts |
| A program (executable file) | it is started; the status line says "Program started" |
| Any other file | the application that declares its extension, otherwise the Editor |

An application declares the extensions it can open in its manifest (`app.txt`, `opens` line). A `.txt` file, like any file no application claims, opens in the Editor.

## Creating a folder

1. Go to the folder that must contain it.
2. **File > New Folder** (Ctrl+N).

The folder is named "New Folder", or "New Folder 2", "New Folder 3"... if the name is already taken.

## Renaming

1. Choose the file or folder.
2. **File > Rename…**, the **F2** key, or **Rename…** in the right-click menu.
3. A dialog asks for the new name, with the old one already typed and selected: type it, then **OK** (or Enter).

The renamed item stays chosen. A name cannot be empty or contain `/ \ : * ? " < > |` (cards and drives, in FAT32 or exFAT, refuse them), and two items of the same folder cannot have the same name, ignoring case: in these cases, a message says so and the dialog comes back. You can change only the case of a name ("photo.jpg" to "Photo.jpg").

## Deleting

1. Choose the file or folder.
2. **File > Delete**, or the Delete key.
3. A small menu asks for confirmation: **Delete "name"** (for a folder: **Delete the folder "name" and its contents**) or **Cancel**.

Deleting is final: Files has no trash (on Google Drive, what you delete goes to the Drive's trash). A folder is deleted with everything in it, in the background, like a copy: the status line turns into a progress strip ("file 3 of 120") with a **Cancel** button, which stops the deletion (what is already deleted stays deleted). If a file cannot be deleted, a message tells which one and why.

## Copying and moving

1. Select the file or folder.
2. **Edit > Copy** (Ctrl+C) to copy it, or **Edit > Cut** (Ctrl+X) to move it; also in the right-click menu.
3. Go to the destination folder, on the same volume or another one (a USB drive, a network share, Google Drive…).
4. **Edit > Paste** (Ctrl+V), or **Paste** in the menu of a right-click on an empty spot.

You can also **drag** an item out of the window, and **drop** files on Files (from another Files window, for example): they go into the folder you release them on, otherwise into the current folder. Dropping **copies**; hold **Shift** when releasing to **move** instead.

Moving on the same volume is instant. Otherwise Files copies, then (for **Cut**) deletes the original. Pasting into the original folder makes a copy named "name (copy)". If an item with the same name already exists, a message asks whether to **replace** it (a folder: put its contents into it), skip it (**No**) or stop everything (**Cancel**).

Copying happens **in the background**: you keep working in Files (change folder, open, rename, paste again) while it runs. A copy that takes more than a moment turns the status line into a **progress strip**: the current item (and "file 3 of 120" for a folder), a bar, the bytes copied out of the total, the speed and the time left. What you paste during a copy is **queued** and runs next ("2 waiting"). Even to or from a network share or Google Drive, where a read or a write can take time, the window stays responsive. The strip's **Cancel** button stops the copy at the end of the current chunk (1 MB) and drops the queued ones: the half-copied file is deleted, the items already copied stay, and for **Cut** the originals are not deleted. To Google Drive, a file is sent to Google when it is closed ("Finishing…"): this step cannot be interrupted, cancelling waits for its end. Closing Files during a copy asks whether to stop it. A folder that is slow to read (Google Drive during a copy, a distant server) shows up when it arrives ("Loading…" in the status line): meanwhile the window stays usable.

If an error stops the copy (no space left, read-only volume, a file over 4 GB to a FAT32 USB drive…), a message tells which file and why.

## Two views side by side

The two-column button in the header, just left of the icons / list switch (or **View > Two views**), splits the window into **two independent views**, each in its own folder, with its own selection and presentation (icons or list). Each view has a banner: the folder name, its path and the free space of its volume. The **active view** has its banner in the accent colours; a click anywhere in a view makes it active. The header (arrows, breadcrumb), the left panel and the status line follow the active view; the other view keeps its selection, greyed.

- **Edit > Copy to the other view** and **Move to the other view** (also in the right-click menu, with the destination folder's name): done at once, without asking, unless an item with the same name would be replaced.
- **Dragging** from one view to the other copies; with **Shift**, it moves. The view you drop on becomes the active one.
- The separator between the two views can be moved with the mouse.
- The same button goes back to **a single view** (the active view stays).

Several items can be selected: **Ctrl+click** adds or removes one, **Shift+click** selects a range, **Ctrl+A** selects all; copy, cut, drag and delete act on the whole selection.

The two views, their folders and the separator position are remembered for next time.

## Choosing the view

- **Icons**: a grid of large icons, the name under each one.
- **List**: one row per item, with the **Name**, **Size** and **Type** columns (Folder, Application, File).

Choose it in the **View** menu or with the small two-segment control in the header. In list view:

- clicking a column title sorts by that column; a second click reverses the order (a ▲ or ▼ arrow shows it);
- widen a column by dragging the separator between two titles.

The view and the column widths are remembered for next time.

## Removing a USB drive

Before unplugging a drive, unmount it so that everything is written:

- click its eject button, in the left panel;
- or, while in a folder of the drive, **Volume > Unmount Current Volume**.

The status line says "Volume unmounted". If a drive is removed without being unmounted while you were in it, Files goes back to **/** ("Volume removed: back to /").

## Connecting to a server (network share)

**Volume > Connect to Server…** (Ctrl+K) opens a folder shared by a Windows PC, a Mac, a NAS or another Korin (SMB):

- **Address**: `smb://192.168.0.9/Share` or `\192.168.0.9\Share` (a folder may follow); the list offers the servers already known;
- **User** and **Password**: an account of the server (`name`, or `DOMAIN
ame`). Left empty, the ones already saved for this share are used.

The share becomes a volume **/net/net0** ("Server") and opens. It is **mounted again at every startup** until you unmount it (eject button, or **Volume > Unmount Current Volume**); the account stays saved (in `/etc/smbfs.ini`) for next time. To share Korin folders with Windows: Settings, **Shares** pane.

## Connecting to Google Drive

**Volume > Connect to Google Drive…** mounts your Google Drive as a volume (on the PC):

- **Drive name**: a short name of your own (`personal`, `work`…), to have several Google accounts;
- **Connect**: the browser opens on Google's page; sign in and allow Korin. The waiting window closes by itself.

The Drive becomes a volume **/net/net0** ("Google Drive") and opens, **readable and writable**: copy, create, rename, move and delete as anywhere else. A file is read as a stream (no need to wait for the whole file); a written file is sent to Google when the program closes it. Deleting moves to the Google Drive **trash** (30 days). Google Docs, Sheets and Slides do not show. The Drive is mounted again at every startup until you unmount it; no password is kept, only an encrypted token (in `HOME:/oauth`).

## Menus and shortcuts

| Menu | Command | Shortcut |
|---|---|---|
| File | Open | Enter |
| File | New Folder | Ctrl+N |
| File | Rename… | F2 |
| File | Delete | Delete |
| File | Refresh | F5 or Ctrl+R |
| File | Close | Ctrl+W |
| Edit | Cut | Ctrl+X |
| Edit | Copy | Ctrl+C |
| Edit | Paste | Ctrl+V |
| Go | Parent Folder | Backspace |
| Go | Go to Location… | Ctrl+L |
| Go | the quick access places, / | |
| View | Icons, List, Two Views, All Volumes | |
| View | Hidden Files (a name that starts with ".", or the FAT hidden attribute) | Ctrl+H |
| Volume | Connect to Server… | Ctrl+K |
| Volume | Connect to Google Drive… | |
| Volume | Unmount Current Volume | |

With the keyboard, in the content:

| Key | Effect |
|---|---|
| Up and Down arrows | previous or next item (one row in icon view) |
| Left and Right arrows | previous or next item, in icon view |
| Home, End | first or last item |
| Enter | open |
| Backspace | parent folder |
| F2 | rename |
| Delete | delete, after confirmation |

## Quick access places

The quick access list comes from the file `/etc/places.ini`, one line per place, in order:

```sh
Accueil = HOME:/
Musiques = HOME:/music
Applications = /apps
```

On the left the displayed name, on the right the folder. A folder that does not exist is not shown. The icon comes from the folder: a house for **HOME:/** (the home folder), a note for `music`, a picture for `pictures`, an arrow for `downloads`, a set square and a pencil for `projects`, a page for `documents`, a film for `video`, a grid for **APPS:/** (`/apps`), linked dots for **PUBLIC:/** (`/home/public`, the shared folder), a folder for the others. Files creates the missing ones among these home folders when it starts. In the home folder, they also carry their icon on the folder, in the icon view; folders take the accent color of the theme. You can edit this file in the Editor; **File > Refresh** reads the list again.

## Tips

- **Refresh** (F5 or Ctrl+R) reads the folder and the volumes again, without losing the chosen item: useful after copying a file from the [Terminal](help:terminal).
- To copy many files chosen by a pattern (`*.jpg`), the `cp` command of the [Terminal](help:terminal) is still handy; Files copies one item at a time (a whole folder counts as one).
- Installed applications are in **/apps** (the **Applications** quick access); [Software](help:pkgman) installs and removes them.
