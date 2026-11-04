# Developer Guide

Korin OS is written in AArch64 assembly, but its apps can be written in **C** and **C++**: GCC 14 runs inside the system itself (`/gcc`), and every shared library of the system, a **kit**, is called from C like an ordinary function. This guide shows how to write an app, a service or a kit, and describes every kit with complete examples to copy and paste: every example of these pages compiles as it is with the OS toolchain.

## Getting Started

1. [Creating an App](app_en.md): the tools, the folder of an app, `app.txt`, building on Korin OS or on the PC.
2. [The C++ App Skeleton](skeleton_en.md): a complete 150-line app to copy, which follows the desktop theme.
3. [The C Library and the POSIX Layer](libc_en.md): files, threads, processes, sockets, what works and what is missing.

## The Kits

| Kit | For | C header |
|---|---|---|
| [UIKit](uikit_en.md) | windows, events, drawing, text, widgets, dialogs, menus, clipboard, images | `uikit.h` |
| [AudioKit](audiokit_en.md) | sound: playback, capture, formats, mixer, FM synthesis and MIDI | `audiokit_abi.h` |
| [MediaKit](mediakit_en.md) | video: WebM, MP4, VP8, VP9, AV1 | `mediakit.h` |
| [GpuKit](gpukit_en.md) | the V3D GPU of the Raspberry Pi, 3D | `gpukit_abi.h` |
| [NetKit](netkit_en.md) | network: sockets, DNS, status, time, Wi-Fi | `netkit_abi.h` |
| [GpioKit](gpiokit_en.md) | the 40-pin header: GPIO, PWM, I2C, SPI | `gpiokit_abi.h` |
| [ArchiveKit](archivekit_en.md) | compression, ZIP, tar, gzip | `archivekit_abi.h` |
| [DocumentKit](documentkit_en.md) | office formats: XML, encodings, OOXML, ODF, CSV | `documentkit_abi.h` |
| [PrinterKit](printerkit_en.md) | printing: dialog, pages, PDF, network printers | `printerkit.h` |
| [EmuKit](emukit_en.md) | the base of the emulators: window, timing, sound, gamepads | `emukit.h` |

AppKit, the base kit (files, launching programs, strings, languages), is already used by the C library: a C program does not have to open it ([libc](libc_en.md)).

## Going Further

- [Writing a Service](service_en.md): a program of `SD0:/sys` that answers the others under a name.
- [The IPC Functions](ipc_en.md): named services, channels, shared memory, waiting.
- [Writing a Kit](kit_en.md): a library shared by all programs.

## Help for an App

An app has help when its folder holds `help_fr.md` or `help_en.md` (Markdown). While the app is in front, the menu bar then adds a **Help** menu that opens this file in the help viewer; the file of the system language comes first (`lang` in `SD0:/etc/system.ini`, `fr` without the key), the other one if it is missing. If the app already publishes a menu named **Help**, the help lines go at the top of it.

```
myapp/
  main.cpp
  app.txt
  help_fr.md
  help_en.md
  help/
    window.png
```

`pibuild` and `tools/office/build.sh` put `help_*.md` and the `help/` folder next to the executable. What the viewer can show:

| Write | For |
|---|---|
| `#`, `##`, `###` | headings (the contents list, on the left, lists them) |
| `**bold**`, `*italic*`, `` `code` `` | text |
| `- item`, `1. item` | lists, nested by two spaces |
| `> text` | a note |
| three backquotes, then the code, then three backquotes | a code block, with its **Copy** button |
| `\| a \| b \|` then `\|---\|---\|` | a table |
| `![caption](help/window.png)` alone on its line | a PNG or BMP image, shrunk to the page width |
| `[text](other_en.md#heading)` | a link to a page, to a heading of a page |
| `[text](help:files)`, `[text](app:files)` | the help of another app; launching an app |
| `[text](https://...)` | a web page, opened in Jet |

A heading is targeted by its anchor: lower case, spaces changed to `-`, punctuation removed (`## Help for an App` gives `#help-for-an-app`).

> **Note**: an English page may link `other_en.md`: if the file is missing, the viewer takes `other_fr.md`, and the other way round.
