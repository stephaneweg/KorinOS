# Creating an App

A Korin OS app is a folder `SD0:/apps/<name>.app`: its executable `main`, its manifest `app.txt`, and whatever it needs (data, translations, help). It is written in C or C++, compiled with GCC inside Korin OS itself or on a PC, and goes through the system **kits** for everything about the screen, sound or network.

## The Tools

| Where | What | Command |
|---|---|---|
| Korin OS | GCC 14.2, G++, binutils, GNU make | `/gcc/bin/gcc`, `/gcc/bin/g++`, `/gcc/bin/make` |
| Korin OS | build and install a folder | `pibuild [app\|bin\|sys\|svc] folder name` |
| Korin OS | the same in a window | the **PiBuild** app |
| PC (WSL) | the `aarch64-onyx-elf-14.2` cross toolchain | `sh tools/office/build.sh <name>` |

On Korin OS, `/gcc` is a virtual path (the folder `SD0:/gcc`); a compiled program is a static ELF, linked with the C library (newlib), the POSIX layer (`libposix`) and `libstdc++`. The kits come along: `/gcc/lib/lib<kit>.a` holds the stubs of each kit, `/gcc/include/kits` their headers, and the linker only keeps what the program calls.

## The Folder of an App

```
SD0:/home/projects/hello/       the sources (the folder name gives the app name)
  main.cpp                    or main.c; every .c and .cpp of the folder and its subfolders is compiled
  app.txt                     the manifest
  data/                       copied as is into the app folder (never compiled)
    lang-en.txt               the app texts in English
  help_fr.md, help_en.md      the help (Help menu of the menu bar)
  help/                       its images
```

Once built, the app is in `/apps/hello.app/`: `main`, `app.txt`, the content of `data/`, `help_*.md` and `help/`. The Korin OS menu, the panel launcher and PocketQI find it by themselves.

## The Manifest: app.txt

Lines `key = value`, `#` for a comment (`#` is not allowed inside values).

```
# Manifest of Hello
name        = Hello
category    = Développement
opens       = txt
description = A sample app that says hello.
keywords    = sample hello
help        = The Hello button shows a message.
```

| Key | Role |
|---|---|
| `name` | the displayed name (Korin OS menu, launcher, menu bar), translated by the system dictionary |
| `category` | the category of the launcher and of the Korin OS menu, in French as the system knows them (Accessoires, Bureautique, Création, Développement, Internet, Jeux, Multimédia, Système); without it: Autres (Other) |
| `opens` | the extensions it opens (Files passes it the path as argument) |
| `description`, `keywords`, `help` | what PocketQI knows about the app: one sentence, keywords, help lines (several `help`) |
| `llm.<action>`, `run.<action>` | an action PocketQI can start: its description, then the arguments given to the app |

The app icon is `icon.rgba` in its folder (rendered by `tools/icons` for the system apps); without an icon, the launcher draws one.

## The Smallest Program

A command-line program, started from the Terminal:

```c
#include <stdio.h>

int main (int argc, char **argv)
{
	printf ("Hello from Korin OS! (%d argument%s)\n", argc - 1, argc == 2 ? "" : "s");
	for (int i = 1; i < argc; i++)
		printf ("  %s\n", argv[i]);
	return 0;
}
```

```sh
/gcc/bin/gcc hello.c -o SD0:/home/hello
SD0:/home/hello one two
```

## The Smallest Graphical App

A window in the theme colours, a button, a message. It leaves the top right corner free, where the window buttons are (`FRAME_CAP_W`): Korin OS has no title bar, the top 32 pixels move the window.

```c
#include <uikit.h>

static struct ui_vars ui;

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	const unsigned *jet = (const unsigned *) ui_jetons ();		// the theme colours
	ui_window (360, 160, (long) "Hello");
	ui_fill (0, 0, ui.w, ui.h, jet[0]);				// TJ_FACE: the window background
	ui_text ((long) "A Korin OS app.", 20, 70, jet[7], 15, (long) ui.font, 0);	// TJ_TEXTE1
	long b = ui_pushbutton (20, 100, 140, 32, (long) "Hello", UB_DEFAULT);
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (100);
		if (t == WEV_CLOSE)
			break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			if (uikit_w_event (&code, &taken) == b && code == UN_ACTIVATE)
				ui_message ((long) "Hello", (long) "Hello, world!", UM_OK);
		}
	}
	return 0;
}
```

For a real app (resizable window, menus, translations, drawn view), start from the [skeleton](skeleton_en.md).

## Building on Korin OS

In the Terminal:

```sh
pibuild app SD0:/home/projects/hello hello
```

`pibuild` looks at the folder: `main.cpp`, `main.c`, `hello.cpp` or `hello.c` must exist. It compiles everything with `-O2`, links with all the kits, installs into `/apps/hello.app/main` with `app.txt`, `data/`, `help_*.md` and `help/`; the app then starts by its name from the launcher. Objects stay in `/tmp/pibuild/hello`: next time only what changed is compiled again, headers included. The **PiBuild** app does the same in a window and shows the compiler output.

| Type | Installed in |
|---|---|
| `app` | `/apps/<name>.app/main` (the default) |
| `bin` | `/bin/<name>`, a Terminal command |
| `sys` | `SD0:/sys/<name>`, a system program (system rights: a [service](service_en.md)) |
| `svc` | `SD0:/sys/svc/<name>`, a service started by init |

> **Note**: under QEMU the first C++ compilation takes about twenty seconds (`cc1plus` read from the card); on the Pi, `hello.cpp` compiles in 9 s. `SD0:/etc/preload.txt` can keep the tools in memory.

## Building on the PC

Faster for a big app: the same toolchain, in WSL, from the root of the repository.

```sh
sh tools/gcc/kits.sh
cp -r ~/hello src/apps/hello
sh tools/office/build.sh hello
```

The result goes to `build/office/sdroot/apps/hello.app/`, which `build.ps1` puts on the card. Same options as `pibuild`: what compiles on the PC compiles in Korin OS.

## The Rules of a Korin OS App

- **The interface goes through UIKit** (`ui_*`): windows, widgets, dialogs, menus. No embedded toolkit: UIKit follows the chosen theme and screen size.
- **Colours come from the theme**: `ui_jetons ()` returns the `TJ_*` tokens (background, surface, text, accent...), which change with the desktop theme (Givre, Onyx Noir). Never a hard-coded colour for the interface.
- **The top right corner stays free** over `FRAME_CAP_W` pixels: the window buttons are drawn over the content there.
- **Menus go to the menu bar** (`ui_menu`); Quit, in the menu named after the app, arrives as `WEV_CLOSE`.
- **Texts are translated**: `ui_tr ("French text")` returns the translation in the system language; the app dictionary (`data/lang-en.txt`) is loaded with `ui_tr_load`. Sources are written in French: the French text is the key.
- **An app has its help**: `help_fr.md` and `help_en.md` ([the format](index_en.md#help-for-an-app)).

## See Also

- [The C++ App Skeleton](skeleton_en.md)
- [UIKit](uikit_en.md), [the C library and the POSIX layer](libc_en.md)
- [Writing a Service](service_en.md)
