# Writing a Kit

A **kit** is a library shared by the whole system: loaded once, at the same address in every process, it is called like an ordinary function. UIKit, AudioKit, NetKit are kits. Write one when several programs need the same code, or to hide a [service](service_en.md) behind functions (PrinterKit in front of the `print` service, GpioKit in front of `gpio`).

## What a Kit Is

- A file `SD0:/lib/<name>.so`: an ELF64 assembled by FASMARM, linked at address 0, without any absolute address (everything goes through `adr`).
- Its entry point is its **export table**: `'AKIT'`, the number of functions (its **version**), then the offset of each function.
- The table **only grows**: a function is never removed, moved or renamed; a new one is added at the end. A program asks for a minimum version; an old program works with a newer kit.
- The code is shared by all processes; the variables (the writable segment) are **copied into each process**: each one has its own.
- A loaded kit **stays in memory** until the next boot: after replacing `SD0:/lib/<name>.so`, reboot to try it. The system keeps 16 at most.
- Functions follow AAPCS64: arguments in `x0`–`x7`, result in `x0` (and `x1`, `x2` if needed), `x19`–`x28` preserved.

## The Files

| File | Role |
|---|---|
| `src/kits/<kit>/<kit>.abi` | the ordered list of functions, written once: it builds the kit table and the stubs of its users |
| `src/kits/<kit>/<kit>.asm` | the code |
| `src/kits/<kit>/<kit>.inc` | the public constants (taken into the C header) |

The guide keeps a complete example in `SD0:/apps/help.app/api/demokit/`. Its comments are in French, like every source of Korin OS.

## DemoKit: the .abi

```
; DemoKit : un kit d'exemple (guide du developpeur)
; Table en ajout seul : ne jamais retirer, deplacer ni renommer une ligne.
kit_begin demokit, 'demokit'
kit_fn dk_add			; x0, x1 -> x0 = x0 + x1
kit_fn dk_count			; -> x0 = appels de dk_add par ce process
kit_fn dk_upper			; x0 = chaine : en majuscules (ASCII), sur place -> x0 = longueur
kit_end
```

The comment of each line gives its arguments and its result: it is the documentation of the kit, and `tools/kits/abi2c.py` uses it for the C header.

## DemoKit: the Code

```
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header

segment readable executable
include 'demokit.abi'				; the export table

; x0, x1 -> x0 = x0 + x1 (and one more call)
dk_add:
	adr	x2,dk_calls
	ldr	x3,[x2]
	add	x3,x3,1
	str	x3,[x2]
	add	x0,x0,x1
	ret

; -> x0 = calls of dk_add by this process
dk_count:
	adr	x0,dk_calls
	ldr	x0,[x0]
	ret

; x0 = string: its letters a..z in upper case, in place -> x0 = length
dk_upper:
	mov	x2,x0
	mov	x0,0
.c:	ldrb	w1,[x2,x0]
	cbz	w1,.e
	sub	w3,w1,'a'
	cmp	w3,25
	b.hi	.n
	sub	w1,w1,32
	strb	w1,[x2,x0]
.n:	add	x0,x0,1
	b	.c
.e:	ret

segment readable writeable
dk_calls	dd 0				; (dd: 64 bits with FASMARM)
```

`kit_header` makes the ELF header; including the `.abi` in the code segment puts the table there. With FASMARM, `db` is 8 bits, `dh` 16, `dw` 32 and `dd` 64. A kit may use another one: `kit_use '../../kits/appkit/appkit.abi'` in its code, `kit_data` in its variables, `bl kits_open` before the first call (UIKit imports AppKit this way).

## Building and Installing

On Korin OS, from `SD0:/src/kits/demokit` (the system sources are on the card, `include` finds them):

```sh
mkdir -p SD0:/src/kits/demokit
cp SD0:/apps/help.app/api/demokit/demokit.asm SD0:/apps/help.app/api/demokit/demokit.abi SD0:/src/kits/demokit
cd SD0:/src/kits/demokit
fasmarm demokit.asm SD0:/lib/demokit.so
```

On the PC: put the folder in `src/kits/demokit`; `build.ps1` assembles every `src/kits/<kit>/<kit>.asm` and puts it in `SD0:/lib`. For C, add a line to `tools/gcc/kits.sh`:

```sh
kit demokit "$K/demokit/demokit.inc"
```

`sh tools/gcc/kits.sh` then produces `/gcc/include/kits/demokit_abi.h` (the prototypes and `demokit_kit_open ()`) and `/gcc/lib/libdemokit.a` (the stubs); `pibuild` takes them automatically.

## Calling It from C, without a Generated Header

`<armpi/kit.h>` opens any kit by its name and gives its functions by their rank (the `kit_fn` line, from 0). This is what the generated stubs do; it is also the way to try a brand new kit on Korin OS, where Python (which generates the headers) does not exist.

```c
#include <stdio.h>
#include <armpi/kit.h>

int main (void)
{
	void *k = armpi_kit_open ("demokit", 3);		// at least 3 functions
	if (!k)
	{
		printf ("demokit missing: SD0:/lib/demokit.so, version 3 or more\n");
		return 1;
	}
	long (*dk_add) (long, long) = (long (*) (long, long)) armpi_kit_fn (k, 0);
	long (*dk_count) (void) = (long (*) (void)) armpi_kit_fn (k, 1);
	long (*dk_upper) (char *) = (long (*) (char *)) armpi_kit_fn (k, 2);
	printf ("version %u\n", armpi_kit_version (k));
	printf ("2 + 3 = %ld\n", dk_add (2, 3));
	printf ("40 + 2 = %ld\n", dk_add (40, 2));
	printf ("calls: %ld\n", dk_count ());
	char s[] = "hello, korin";
	long n = dk_upper (s);
	printf ("%s (%ld letters)\n", s, n);
	return 0;
}
```

The same program without the kit installed learns it from `armpi_kit_open`, which returns `NULL`: a program can thus do without an optional kit.

## Calling It from Assembly

```
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
segment readable executable
kit_use '../../kits/demokit/demokit.abi'	; one stub per function
start:	bl	kits_open			; message and exit if the kit is missing
	mov	x0,2
	mov	x1,3
	bl	dk_add				; x0 = 5
	...
segment readable writeable
kit_data					; the import table, filled by kits_open
```

## Rules

- **Never change a meaning**: a published function keeps its arguments and its result. To do otherwise, a new function at the end (`dk_add_ex`).
- **No absolute address**: no `dd label` in a kit (FASMARM emits no relocations); `adr` only.
- **Variables are per process**; what must be common to all goes through a service or a shared memory.
- **A kit does not write to the console** of a graphical program: it returns error codes (negative, like the kernel), and a function that tells them in text (`gp_error` of GpioKit).
- **It assembles identically** with FASMARM on Windows and on Korin OS: no external tool.
- **Names have a prefix** of their own (`ui_`, `ak_`, `gp_`, `dk_`): a program cannot define a function with the same name as a function of a kit it uses.

## See Also

- [Writing a Service](service_en.md): when the code must run in a single process for everyone.
- The system kits: [contents](index_en.md#the-kits).
