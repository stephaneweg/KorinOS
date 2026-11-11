# Assemble les applications (build\apps\*.elf) puis le noyau (build\kernel8.img)
#   .\build.ps1          assemble
#   .\build.ps1 run      assemble puis lance QEMU raspi4b (UART sur la console)
#   .\build.ps1 sd X:    assemble puis copie firmware + noyau sur la carte SD X:
#                        (ecran 800x600 par defaut, comme Onyx sur le PocketTerm ;
#                         -Ecran 640x480 ou 1024x768 sinon)
#   .\build.ps1 min D   assemble puis prepare dans le dossier D la carte minimale (tools/mincard.py)
#   .\build.ps1 test     assemble puis demarre QEMU sans affichage et attend sur l'UART les marqueurs
#                        d'un bon demarrage (tools/ci/boot_test.py, comme la CI : docs/CI.md)
# QEMU : $env:ARMPI_QEMU s'il est defini, sinon C:\Program Files\qemu\qemu-system-aarch64.exe
param([string]$Action = "", [string]$Drive = "", [string]$Ecran = "800x600")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$fasm = Join-Path $root "tools\fasmarm\fasmarm.exe"
$qemu = if ($env:ARMPI_QEMU) { $env:ARMPI_QEMU } else { "C:\Program Files\qemu\qemu-system-aarch64.exe" }
$out  = Join-Path $root "build\kernel8.img"
$apps = Join-Path $root "build\apps"
$libs = Join-Path $root "build\lib"
$svc  = Join-Path $root "build\svc"
foreach ($d in @($apps, $libs, $svc)) { New-Item -ItemType Directory -Force $d | Out-Null }

# constantes redefinies avec des valeurs differentes dans un meme programme :
# FASMARM ne le signale pas (tools/check_constants.py) -> arret
& python -I (Join-Path $root "tools\check_constants.py") $root
if ($LASTEXITCODE -ne 0) { throw "echec : constantes redefinies (tools\check_constants.py)" }

# Un programme par dossier : src\<categorie>\<nom>\<nom>.asm, assemble dans son
# dossier ; la categorie decide de sa place sur la carte :
#   src\apps\<nom> -> build\apps\<nom>.elf -> /sd/apps/<nom>.app (main + app.txt)
#   src\bin\<nom>  -> build\apps\<nom>.elf -> /sd/bin/<nom>
#   src\sys\<nom>  -> build\apps\<nom>.elf -> /sd/sys/<nom>
#   src\svc\<nom>  -> build\svc\<nom>.elf  -> /sd/sys/svc/<nom>
#   src\kits\<kit> -> build\lib\<kit>.so   -> /sd/lib/<kit>.so
# (src\svc\inc, src\svc\modules : pas de <nom>.asm, ignores ici)
function Assembler([string]$cat, [string]$dst, [string]$ext) {
  $noms = @()
  foreach ($d in Get-ChildItem (Join-Path $root "src\$cat") -Directory) {
    if (-not (Test-Path (Join-Path $d.FullName "$($d.Name).asm"))) { continue }
    Push-Location $d.FullName
    try {
      & $fasm "$($d.Name).asm" (Join-Path $dst "$($d.Name)$ext") | Select-Object -Last 1 | Write-Host
      if ($LASTEXITCODE -ne 0) { throw "echec : $cat\$($d.Name)" }
    } finally { Pop-Location }
    $noms += $d.Name
  }
  return ,$noms
}
$p_apps = Assembler "apps" $apps ".elf"
$p_bin  = Assembler "bin"  $apps ".elf"     # dont FASMARM natif (tools/x86toa64.py)
$p_sys  = Assembler "sys"  $apps ".elf"
$p_kits = Assembler "kits" $libs ".so"      # kits (docs/KITS.md)
$p_svc  = Assembler "svc"  $svc  ".elf"     # services (docs/MICRO-NOYAU.md)

# modules de demarrage (initramfs, docs/MICRO-NOYAU.md : init, sd, vfs,
# fat32, appkit.so, init.txt) -> build\modules\<nom>.mod
$mods = Join-Path $root "build\modules"
Remove-Item $mods -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $mods | Out-Null
Push-Location (Join-Path $root "src\svc\modules")
try {
  foreach ($src in Get-ChildItem *.asm) {
    & $fasm $src.Name (Join-Path $mods ($src.BaseName + ".mod")) | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "echec : modules\$($src.Name)" }
  }
} finally { Pop-Location }

Push-Location (Join-Path $root "src\kernel")
try { & $fasm kernel.asm $out; if ($LASTEXITCODE -ne 0) { throw "echec : kernel.asm" }
      # noyau du Raspberry Pi 5 (arch/pi5, non teste sur materiel) : toujours
      # assemble, pour que les deux cartes restent constructibles
      & $fasm -d ARCH=pi5 kernel.asm (Join-Path $root "build\kernel_2712.img") | Select-Object -Last 1
      if ($LASTEXITCODE -ne 0) { throw "echec : kernel.asm (pi5)" } }
finally { Pop-Location }

# carte SD de test : applications + polices dans build\sdroot, puis image disque
$sdroot = Join-Path $root "build\sdroot"
# (le modele suivi, skel\sd0 ; tout le reste de build\sdroot est refait ici)
Remove-Item $sdroot -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "skel\sd0") $sdroot -Recurse -Force
New-Item -ItemType Directory -Force (Join-Path $sdroot "fonts") | Out-Null
# Les executables n'ont pas d'extension sur la carte :
#   programmes en ligne de commande -> /sd/bin/<nom>
#   services (docs/MICRO-NOYAU.md) -> /sd/sys/svc/<nom>
#   programmes du systeme -> /sd/sys/<nom> : Elegant (serveur graphique, lance par
#     le noyau), la session du bureau (/sd/etc/session : fond, barre de menus, dock)
#   applications -> /sd/apps/<nom>.app/ : main (l'executable), app.txt (manifeste :
#     src/apps/<nom>/app.txt), et sa configuration interne s'il en a une
# (la categorie vient du dossier source : voir Assembler plus haut)
foreach ($d in @("apps","bin","sys")) {                  # (genere : refait a neuf)
  Remove-Item (Join-Path $sdroot $d) -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $sdroot $d) | Out-Null
}
foreach ($n in $p_sys) { Copy-Item (Join-Path $apps "$n.elf") (Join-Path $sdroot "sys\$n") }
foreach ($n in $p_bin) { Copy-Item (Join-Path $apps "$n.elf") (Join-Path $sdroot "bin\$n") }
foreach ($n in $p_apps) {
  $pq = Join-Path $sdroot "apps\$n.app"
  New-Item -ItemType Directory -Force $pq | Out-Null
  Copy-Item (Join-Path $apps "$n.elf") (Join-Path $pq "main")
  $man = Join-Path $root "src\apps\$n\app.txt"
  if (Test-Path $man) { Copy-Item $man (Join-Path $pq "app.txt") }
  else { Write-Warning "manifeste absent : $man" }
}
# services -> /sd/sys/svc/<nom>
New-Item -ItemType Directory -Force (Join-Path $sdroot "sys\svc") | Out-Null
foreach ($n in $p_svc) { Copy-Item (Join-Path $svc "$n.elf") (Join-Path $sdroot "sys\svc\$n") }
# dossiers de l'utilisateur (vides : git ne les garde pas) : SD0:/home
foreach ($d in @("documents","music","pictures","video")) {
  New-Item -ItemType Directory -Force (Join-Path $sdroot "home\$d") | Out-Null
}
# FM Tracker : ses instruments, ses mots, et les morceaux d'exemple dans SD0:/home/music/fms
$fmt = Join-Path $root "src\apps\fmtracker"
Copy-Item (Join-Path $fmt "ins") (Join-Path $sdroot "apps\fmtracker.app\ins") -Recurse -Force
Copy-Item (Join-Path $fmt "lang") (Join-Path $sdroot "apps\fmtracker.app\lang") -Recurse -Force
New-Item -ItemType Directory -Force (Join-Path $sdroot "home\music\fms") | Out-Null
Copy-Item (Join-Path $fmt "songs\*") (Join-Path $sdroot "home\music\fms") -Force
# init.txt (les services lances par init) : sur la carte aussi, pour
# mkinitramfs (SD0:/etc/initramfs.txt)
Copy-Item (Join-Path $root "src\svc\modules\init.txt") (Join-Path $sdroot "etc\init.txt") -Force
# kits -> /sd/lib
New-Item -ItemType Directory -Force (Join-Path $sdroot "lib") | Out-Null
foreach ($n in $p_kits) { Copy-Item (Join-Path $libs "$n.so") (Join-Path $sdroot "lib") -Force }
# kits precompiles (code C porte : mediakit, tools/mediakit ; hors du selfhost)
Remove-Item (Join-Path $sdroot "lib\tlskit.so") -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "prebuilt\*.so") (Join-Path $sdroot "lib") -Force
# programmes C precompiles (couche POSIX, src/lib/posix, tools/posix/build.sh ; bash : tools/bash/build.sh ;
# rg : tools/rg/build.sh, docs/RG.md ;
# hors du selfhost)
foreach ($n in @("posixtest","posixtest-cxx","ttytest","jobtest","bash","rg")) {
  $f = Join-Path $root "prebuilt\$n"
  if (Test-Path $f) { Copy-Item $f (Join-Path $sdroot "bin") -Force }
}
# modules d'essai du groupe dl de posixtest (dlopen, docs/POSIX.md) : a cote de lui, pas dans lib (les kits)
$dlt = Join-Path $root "prebuilt\dltest"
if (Test-Path $dlt) { Copy-Item (Join-Path $dlt "*.so") (Join-Path $sdroot "bin") -Force }
# WebKit (docs/WEBKIT.md) : construit dans WSL, hors du depot et du selfhost ; tools/webkit/stage.sh
# depose ses programmes dans build\webkit\sdroot (bin\jsc, bin\wctest, apps\jet.app...) : recopies
# sur la carte s'ils y sont
$wk = Join-Path $root "build\webkit\sdroot"
if (Test-Path $wk) { Copy-Item (Join-Path $wk "*") $sdroot -Recurse -Force }
# OpenSSL (docs/OPENSSL.md) : essais construits dans WSL par tools/openssl/build.sh, hors du depot
# et du selfhost
$os = Join-Path $root "build\openssl\sdroot"
if (Test-Path $os) { Copy-Item (Join-Path $os "*") $sdroot -Recurse -Force }
# git (docs/GIT.md) : construit dans WSL par tools/git/build.sh, hors du depot et du selfhost
# (bin\git, bin\git-core, etc\gitconfig, etc\git-core)
$gt = Join-Path $root "build\git\sdroot"
if (Test-Path $gt) { Copy-Item (Join-Path $gt "*") $sdroot -Recurse -Force }
# Suite bureautique (docs/BUREAUTIQUE.md) : construite dans WSL par tools/office/build.sh, hors du selfhost
$of = Join-Path $root "build\office\sdroot"
if (Test-Path $of) { Copy-Item (Join-Path $of "*") $sdroot -Recurse -Force }
# Emulateurs en C++ (docs/EMULATEURS.md) : construits dans WSL par tools/emu/build.sh, hors du selfhost
$av = Join-Path $root "build\avalonia\sdroot"     # (Avalonia : docs/AVALONIA.md)
if (Test-Path $av) { Copy-Item (Join-Path $av "*") $sdroot -Recurse -Force }
$em = Join-Path $root "build\emu\sdroot"
if (Test-Path $em) { Copy-Item (Join-Path $em "*") $sdroot -Recurse -Force }
# SDL, SuperTux et SuperTuxKart (docs/SDL.md) : construits dans WSL par tools/sdl
$sd = Join-Path $root "build\sdl\sdroot"
if (Test-Path $sd) { Copy-Item (Join-Path $sd "*") $sdroot -Recurse -Force }
# GCC et G++ (docs/GCC.md) : construits dans WSL par tools/gcc/build.sh, hors du depot et du
# selfhost ; SD0:/gcc, vu des programmes comme /gcc (SD0:/etc/paths.txt)
$gc = Join-Path $root "build\gcc\sdroot"
# l'heure de depart de la carte (SD0:/etc/lastclock : init2) : secondes UTC, decalage local
$maintenant = [DateTimeOffset]::Now
$heure = "$($maintenant.ToUnixTimeSeconds()) $([int]$maintenant.Offset.TotalSeconds)"
Set-Content -Path (Join-Path $sdroot "etc\lastclock") -Encoding ascii -Value $heure
if (Test-Path $gc) { Copy-Item (Join-Path $gc "*") $sdroot -Recurse -Force }
# .NET (tools/dotnet) : construit dans WSL, hors du depot et du selfhost
$dn = Join-Path $root "build\dotnet\sdroot"
# (un ancien Courier en C#, d'avant le port en C++ : il ecraserait celui de build\office)
$ancien = Join-Path $dn "apps\courier.app"
if (Test-Path $ancien) { Remove-Item $ancien -Recurse -Force }
if (Test-Path $dn) { Copy-Item (Join-Path $dn "*") $sdroot -Recurse -Force }
# (l'hote relie par tools/avalonia/build-host.sh, a la libposix du moment, passe devant celui de build\dotnet)
$hote = Join-Path $av "dotnet\dotnet"
if (Test-Path $hote) { Copy-Item $hote (Join-Path $sdroot "dotnet") -Force }
# SoundFont General MIDI d'Onyx (GeneralUser GS, 32 Mo, hors de git) : le synthe MIDI d'AudioKit
# (SD0:/res/soundfonts) ; sans elle, il joue sur sa banque FM
# (depuis le clone d'Onyx de tools/extra/fetch.sh, ou $env:ONYX_DIR)
$sf = if ($env:ONYX_DIR) { Join-Path $env:ONYX_DIR "sdcard\res\soundfonts" } else { Join-Path $root "build\onyx\sdcard\res\soundfonts" }
if (Test-Path $sf) {
  New-Item -ItemType Directory -Force (Join-Path $sdroot "res\soundfonts") | Out-Null
  Copy-Item (Join-Path $sf "*") (Join-Path $sdroot "res\soundfonts") -Force
}
# Chansons de Koton Studio de l'utilisateur (hors de git), ouvertes par Koton : seulement si
# $env:ARMPI_KOTON_SONGS nomme leur dossier
$sq = $env:ARMPI_KOTON_SONGS
if ($sq -and (Test-Path (Join-Path $sq "*.sq"))) {
  New-Item -ItemType Directory -Force (Join-Path $sdroot "home\koton\songs") | Out-Null
  Copy-Item (Join-Path $sq "*.sq") (Join-Path $sdroot "home\koton\songs") -Force
}
# Morceaux d'exemple de Koton Studio (tools/koton/make_demos.py ; paquet koton-demos)
$kd = Join-Path $root "src\apps\koton\demos"
if (Test-Path (Join-Path $kd "*.kson")) {
  New-Item -ItemType Directory -Force (Join-Path $sdroot "home\koton\demos") | Out-Null
  Copy-Item (Join-Path $kd "*.kson") (Join-Path $sdroot "home\koton\demos") -Force
}
# sources du systeme sur la carte (assemblables sous ARMPI par /sd/bin/fasmarm,
# fasmarm compris : il se reassemble lui-meme, octet pour octet)
$srcdst = Join-Path $sdroot "src"
Remove-Item $srcdst -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $srcdst | Out-Null
# (sauf src\extra : WebKit et ses dependances, des centaines de Mo de C/C++)
Get-ChildItem (Join-Path $root "src") | Where-Object { $_.Name -ne "extra" } |
  ForEach-Object { Copy-Item $_.FullName $srcdst -Recurse -Force }
# fichiers de test de selfhost (/sd/t) : hello avec la meme arborescence que src/
$tdst = Join-Path $sdroot "t"
Remove-Item $tdst -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $tdst "bin\hello") | Out-Null
Copy-Item (Join-Path $root "src\bin\hello\hello.asm") (Join-Path $tdst "bin\hello") -Force
Copy-Item (Join-Path $root "src\inc") $tdst -Recurse -Force
Copy-Item (Join-Path $root "src\kits") $tdst -Recurse -Force
Copy-Item (Join-Path $root "tools\fasmarm\armdoc\InstructionFormatsARM64.asm"), (Join-Path $root "tools\fasmarm\armdoc\InstructionFormatsTHUMB32.asm") $tdst -Force
Copy-Item (Join-Path $root "ref\fonts\*.ttf") (Join-Path $sdroot "fonts") -Force
# firmware WiFi CYW43455 (charge par le noyau depuis /sd/firmware)
New-Item -ItemType Directory -Force (Join-Path $sdroot "firmware") | Out-Null
Copy-Item (Join-Path $root "firmware\wifi\*") (Join-Path $sdroot "firmware") -Force
# icones (tools/icons/build.py les rend, elles sont commitees : pas besoin de Python ici) : dans le dossier de
# l'application, elles partent avec son paquet ; celles sans dossier (applets, corbeille) dans /res/icons
New-Item -ItemType Directory -Force (Join-Path $sdroot "res\icons") | Out-Null
foreach ($f in Get-ChildItem (Join-Path $root "tools\icons\rendered\*.rgba")) {
  $pq = Join-Path $sdroot "apps\$($f.BaseName).app"
  if (Test-Path $pq) { Copy-Item $f.FullName (Join-Path $pq "icon.rgba") -Force }
  else { Copy-Item $f.FullName (Join-Path $sdroot "res\icons") -Force }
}
# l'aide de chaque application (lue par SD0:/apps/help.app, ouverte par le menu Aide de la barre de menus) :
# help_fr.md, help_en.md et leurs images (help/) de src\apps\<nom> ou src\extra\onyxports\<nom>, dans son dossier
foreach ($d in (Get-ChildItem (Join-Path $root "src\apps"), (Join-Path $root "src\extra\onyxports") -Directory)) {
  $pq = Join-Path $sdroot "apps\$($d.Name).app"
  if (-not (Test-Path $pq)) { continue }
  Get-ChildItem $d.FullName -Filter "help_*.md" | ForEach-Object { Copy-Item $_.FullName $pq -Force }
  $im = Join-Path $d.FullName "help"
  if (Test-Path $im) { Copy-Item $im $pq -Recurse -Force }
}
# les dictionnaires : skel et les lang/<xx>.txt de chaque programme (docs/LANGUES.md)
& python -I (Join-Path $root "tools\lang\merge.py") (Join-Path $sdroot "res\lang")
if ($LASTEXITCODE -ne 0) { throw "echec : tools\lang\merge.py" }
# la base des paquets de la carte (docs/PAQUETS.md) : generee ici, jamais a la main
& python -I (Join-Path $root "tools\pkg\mkrepo.py") --db-only --sd $sdroot
if ($LASTEXITCODE -ne 0) { throw "echec : tools\pkg\mkrepo.py" }
# image FAT32 d'essai des services de fichiers (imgblk + fat32 : IMG0:)
$essai = Join-Path $root "build\test_img"
New-Item -ItemType Directory -Force $essai | Out-Null
Set-Content -Path (Join-Path $essai "readme.txt") -Encoding ascii -Value "Bonjour depuis une image FAT32 servie par fat32 + imgblk."
& python -I (Join-Path $root "tools\mkdisk.py") (Join-Path $root "build\test.img") --fat32 $essai --taille 8 | Out-Null
Copy-Item (Join-Path $root "build\test.img") (Join-Path $sdroot "test.img") -Force
# image exFAT d'essai (imgblk + fatfs en exFAT : IMG1:)
$essaix = Join-Path $root "build\test_exfat"
New-Item -ItemType Directory -Force $essaix | Out-Null
Set-Content -Path (Join-Path $essaix "readme.txt") -Encoding ascii -Value "Image exFAT d'essai (IMG1:), servie par imgblk + fatfs."
& python -I (Join-Path $root "tools\mkdisk.py") (Join-Path $root "build\test_exfat.img") --exfat $essaix --taille 16 | Out-Null
Copy-Item (Join-Path $root "build\test_exfat.img") (Join-Path $sdroot "test_exfat.img") -Force
# la partition exFAT : skel\sd1 et les donnees d'exfattest (tools/exfatdata.py)
$exroot = Join-Path $root "build\exfat_root"
& python -I (Join-Path $root "tools\exfatdata.py") (Join-Path $root "skel\sd1") $exroot
& python -I (Join-Path $root "tools\mkdisk.py") (Join-Path $root "build\sd.img") --fat32 $sdroot --exfat $exroot | Out-Null
if ($LASTEXITCODE -ne 0) { throw "echec : tools\mkdisk.py (build\sd.img)" }

# noyaux de la carte : resolution choisie (QEMU garde 1024x768)
function Noyaux-Carte {
  $w, $h = $Ecran -split "x"
  $sdk = Join-Path $root "build\kernel8_sd.img"
  $sdk5 = Join-Path $root "build\kernel_2712_sd.img"
  Push-Location (Join-Path $root "src\kernel")
  try { & $fasm -d "ECRAN_W=$w" -d "ECRAN_H=$h" kernel.asm $sdk | Select-Object -Last 1 | Write-Host
        if ($LASTEXITCODE -ne 0) { throw "echec : kernel.asm ($Ecran)" }
        & $fasm -d ARCH=pi5 -d "ECRAN_W=$w" -d "ECRAN_H=$h" kernel.asm $sdk5 | Select-Object -Last 1 | Write-Host
        if ($LASTEXITCODE -ne 0) { throw "echec : kernel.asm (pi5, $Ecran)" } }
  finally { Pop-Location }
  return @($sdk, $sdk5)
}

switch ($Action) {
  "run" {
    & $qemu -M raspi4b `
      -kernel $out -dtb (Join-Path $root "firmware\bcm2711-rpi-4-b.dtb") -serial stdio `
      -drive "file=$(Join-Path $root 'build\sd.img'),if=sd,format=raw" -device usb-kbd -device usb-tablet `
      -device usb-net,netdev=n0 -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:8080-:80" `
      -device usb-audio,audiodev=snd0 -audiodev "dsound,id=snd0"
  }
  "test" {
    $env:ARMPI_QEMU = $qemu
    & python -I (Join-Path $root "tools\ci\boot_test.py")
    if ($LASTEXITCODE -ne 0) { throw "echec : demarrage sous QEMU (build\ci-uart.log)" }
  }
  "min" {
    if (-not $Drive) { throw "Usage : .\build.ps1 min sdcard [-Ecran 640x480]" }
    $sdk, $sdk5 = Noyaux-Carte
    # pkg, pkgd, pkgman viennent de WSL (tools/pkg/build.sh) ; un worktree prend ceux du depot principal
    $off = Join-Path $root "build\office\sdroot"
    if (-not (Test-Path $off)) { $off = Join-Path ($root -replace '[\\/]\.claude[\\/]worktrees[\\/].*$', '') "build\office\sdroot" }
    & python -I (Join-Path $root "tools\mincard.py") --sd $sdroot --out $Drive --kernels "$sdk,$sdk5" --overlay $off
    if ($LASTEXITCODE -ne 0) { throw "echec : tools\mincard.py" }
  }
  "sd" {
    if (-not $Drive) { throw "Usage : .\build.ps1 sd E: [-Ecran 640x480]" }
    $sdk, $sdk5 = Noyaux-Carte
    Copy-Item (Join-Path $root "firmware\start4.elf"), (Join-Path $root "firmware\fixup4.dat"),
              (Join-Path $root "firmware\*.dtb"), (Join-Path $root "boot\config.txt") -Destination "$Drive\"
    # Pi 5 : son firmware est dans l'EEPROM, il lit seulement les bcm2712*.dtb et l'overlay
    New-Item -ItemType Directory -Force "$Drive\overlays" | Out-Null
    Copy-Item (Join-Path $root "firmware\overlays\*") -Destination "$Drive\overlays\"
    Copy-Item $sdk -Destination "$Drive\kernel8.img"
    Copy-Item $sdk5 -Destination "$Drive\kernel_2712.img"     # Pi 5 ([pi5] de config.txt)
    # modules de demarrage : a la racine, charges par le firmware (initramfs)
    Remove-Item "$Drive\essai1.mod","$Drive\essai2.mod" -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $root "build\modules\*.mod") -Destination "$Drive\"
    # reglages d'une carte plus ancienne (a la racine) : deplaces dans /etc,
    # sans jamais ecraser ceux deja presents dans /etc
    New-Item -ItemType Directory -Force "$Drive\etc" | Out-Null
    foreach ($cfg in @("options.txt","wifi.txt","demarrage.txt","clock.cfg")) {
      if ((Test-Path "$Drive\$cfg") -and -not (Test-Path "$Drive\etc\$cfg")) { Move-Item "$Drive\$cfg" "$Drive\etc\$cfg" }
    }
    # anciens noms francais de /etc : renommes (le contenu est garde)
    # (et les donnees des applications renommees, a la racine)
    foreach ($ren in @(@("etc\demarrage.txt","etc\startup.txt"), @("etc\montages.txt","etc\mounts.txt"),
                       @("etc\horloge.cfg","etc\clock.cfg"), @("etc\applis.ini","etc\applist.ini"),
                       @("etc\wifi.exemple.txt","etc\wifi.example.txt"),
                       @("dessin.bmp","paint.bmp"), @("cibles.sco","targets.sco"))) {
      $old = "$Drive\$($ren[0])"; $new = "$Drive\$($ren[1])"
      if ((Test-Path $old) -and -not (Test-Path $new)) { Move-Item $old $new }
      Remove-Item $old -ErrorAction SilentlyContinue
    }
    Remove-Item "$Drive\wifi.exemple.txt" -ErrorAction SilentlyContinue
    # anciens noms francais des applications, services, images et modules :
    # supprimes (remplaces par les nouveaux), et renommes dans les reglages
    $apps_fr = @{ bienvenue="welcome"; calculatrice="calculator"; cibles="targets"; dessin="paint";
                  editeur="editor"; fichiers="files"; horloge="clock"; magneto="recorder";
                  moniteur="taskman"; monitor="taskman"; navigateur="browser" }
    foreach ($a in $apps_fr.Keys) { Remove-Item "$Drive\apps\$a.app" -Recurse -Force -ErrorAction SilentlyContinue }
    Remove-Item "$Drive\bin\mesure","$Drive\bin\httpsget","$Drive\lib\tlskit.so","$Drive\gcc\lib\libtlskit.a",
                "$Drive\gcc\include\kits\tlskit_abi.h","$Drive\gcc\aarch64-onyx-elf\armpi\include\tlskit_abi.h","$Drive\sys\svc\essaimat","$Drive\LISEZMOI.TXT","$Drive\essai.img",
                "$Drive\essai_exfat.img","$Drive\liste.mod" -Force -ErrorAction SilentlyContinue
    foreach ($f in @("dock.ini","panel.ini","startup.txt","session","services.txt","applist.ini")) {
      $p = "$Drive\etc\$f"
      if (-not (Test-Path $p)) { continue }
      $lignes = [IO.File]::ReadAllLines($p) | ForEach-Object {
        $l = $_
        if (-not $l.TrimStart().StartsWith("#")) {
          foreach ($a in $apps_fr.Keys) { $l = $l -replace "\b$a\b", $apps_fr[$a] }
          $l = $l -replace "\bessai(_exfat)?\.img\b", 'test$1.img'
        }
        $l
      }
      [IO.File]::WriteAllLines($p, [string[]]$lignes)
    }
    # session : notifyd (les notifications) ajoute s'il manque
    $ses = "$Drive\etc\session"
    if ((Test-Path $ses) -and -not (Select-String -Path $ses -Pattern "notifyd" -Quiet)) {
      Add-Content -Path $ses -Value "SD0:/sys/notifyd" -Encoding ascii
    }
    # session : clipd (le presse-papiers), les applets agenda et stickies, pkgd (les mises a jour), printd (l'impression), clockd (les alarmes) ajoutes s'ils manquent
    foreach ($p in @("clipd","agenda","stickies","pkgd","printd","clockd")) {
      if ((Test-Path $ses) -and -not (Select-String -Path $ses -Pattern $p -Quiet)) {
        Add-Content -Path $ses -Value "SD0:/sys/$p" -Encoding ascii
      }
    }
    # le panel remplace le dock dans la session de la carte (le dock reste dans /sys)
    if (Test-Path $ses) {
      $lignes = [IO.File]::ReadAllLines($ses) | ForEach-Object { $_ -replace '^(\s*)SD0:/sys/dock\s*$', '${1}SD0:/sys/panel' }
      [IO.File]::WriteAllLines($ses, [string[]]$lignes)
    }
    # il ne reste que Givre et Onyx Noir : un autre theme sur la carte redevient Givre
    $thm = "$Drive\etc\theme.txt"
    if ((Test-Path $thm) -and (Select-String -Path $thm -Pattern '^\s*(theme\s*=\s*(Peach|Steel|Sage|Brick|Slate|Milk)|style\s*=\s*(cde|milk))' -Quiet)) {
      Copy-Item (Join-Path $sdroot "etc\theme.txt") $thm -Force
    }
    # les couleurs viennent des jetons de UIKit : celles de l'ancien modele sont retirees
    # (accent, menubar, dock... ecrasaient les jetons) ; une couleur choisie a la main reste
    if (Test-Path $thm) {
      $vieux = '^\s*(style\s*=\s*plat|active\s*=\s*0xE9EDF2|inactive\s*=\s*0xF5F6F8|window\s*=\s*0xF5F6F8|button\s*=\s*auto|field\s*=\s*auto|accent\s*=\s*0x2F74E0|menubar\s*=\s*0xF7F8FA|dock\s*=\s*0xF2F4F7|outline\s*=\s*dark)\s*$'
      $lignes = [IO.File]::ReadAllLines($thm) | Where-Object { $_ -notmatch $vieux }
      [IO.File]::WriteAllLines($thm, [string[]]$lignes)
    }
    # reseau (docs/RESEAU.md) : lignes ajoutees a services.txt et drivers.txt
    # si elles manquent (une ligne mise en commentaire sur la carte reste
    # en commentaire : le reglage de la carte est garde)
    function Ajouter-Lignes([string]$f, [string[]]$lignes) {
      if (-not (Test-Path $f)) { return }
      foreach ($l in $lignes) {
        $cle = ($l -split " = ")[0]
        if (Select-String -Path $f -SimpleMatch -Pattern $cle -Quiet) { continue }
        $txt = [IO.File]::ReadAllText($f)
        if ($txt.Length -gt 0 -and -not $txt.EndsWith("`n")) { $txt += "`r`n" }
        [IO.File]::WriteAllText($f, $txt + $l + "`r`n")
      }
    }
    Ajouter-Lignes "$Drive\etc\services.txt" @("SD0:/sys/svc/vterm", "SD0:/sys/svc/netd", "SD0:/sys/svc/sound", "SD0:/sys/svc/pwmaudio", "SD0:/sys/svc/telnetd",
                                               "SD0:/sys/svc/ftpd", "SD0:/sys/svc/vncd",
                                               "SD0:/sys/svc/rdpd", "SD0:/sys/svc/v3d")
    # une seule categorie Developpement : les tiroirs de la carte suivent les manifestes
    if (Test-Path "$Drive\etc\dock.ini") {
      $lignes = [IO.File]::ReadAllLines("$Drive\etc\dock.ini") | ForEach-Object { $_ -replace '^(\s*tiroir\s*=\s*)Programmation', ("`${1}D" + [char]0xE9 + "veloppement") }
      [IO.File]::WriteAllLines("$Drive\etc\dock.ini", [string[]]$lignes)
    }
    # la suite bureautique : son tiroir, s'il manque, avant Internet
    $dk = "$Drive\etc\dock.ini"
    if ((Test-Path $dk) -and -not (Select-String -Path $dk -Pattern "Bureautique" -Quiet)) {
      $lignes = [IO.File]::ReadAllLines($dk) | ForEach-Object {
        if ($_ -match '^\s*tiroir\s*=\s*Internet') { "tiroir    = Bureautique, letters" }
        $_
      }
      [IO.File]::WriteAllLines($dk, [string[]]$lignes)
    }
    # les lecteurs et le magneto quittent Creation : leur tiroir, s'il manque, avant Bureautique
    if ((Test-Path $dk) -and -not (Select-String -Path $dk -Pattern "Multim" -Quiet)) {
      $lignes = [IO.File]::ReadAllLines($dk) | ForEach-Object {
        if ($_ -match '^\s*tiroir\s*=\s*Bureautique') { "tiroir    = Multim" + [char]0xE9 + "dia, media" }
        $_
      }
      [IO.File]::WriteAllLines($dk, [string[]]$lignes)
    }
    # Jet remplace l'ancien navigateur (browser, retire le 30/10/2026) : tiroir Internet du dock,
    # image prechargee au demarrage
    Remove-Item "$Drive\apps\browser.app" -Recurse -Force -ErrorAction SilentlyContinue
    $dk = "$Drive\etc\dock.ini"
    if (Test-Path $dk) {
      $lignes = [IO.File]::ReadAllLines($dk) | ForEach-Object { $_ -replace '^(\s*tiroir\s*=\s*Internet\s*,\s*)browser\b', '${1}jet' }
      [IO.File]::WriteAllLines($dk, [string[]]$lignes)
    }
    Set-Content -Path "$Drive\etc\lastclock" -Encoding ascii -Value $heure
    Ajouter-Lignes "$Drive\etc\preload.txt" @("SD0:/apps/jet.app/main")
    Ajouter-Lignes "$Drive\etc\paths.txt" @("/dotnet = SD0:/dotnet")
    if (Test-Path (Join-Path $sdroot "dotnet\dllpreload")) { Ajouter-Lignes "$Drive\etc\startup.txt" @("SD0:/dotnet/dllpreload boot") }
    # RAM0 : /tmp de GCC ; ancienne taille de 16 Mo portee a 1 Go (la memoire ne sert qu'a l'usage)
    $mt = "$Drive\etc\mounts.txt"
    if (Test-Path $mt) {
      $lignes = [IO.File]::ReadAllLines($mt) | ForEach-Object { $_ -replace '^(\s*RAM0\s*=\s*)ram:16M\b', '${1}ram:1024M' }
      [IO.File]::WriteAllLines($mt, [string[]]$lignes)
    }
    Ajouter-Lignes "$Drive\etc\drivers.txt" @("soc:genet = SD0:/sys/svc/genet", "soc:wifi = SD0:/sys/svc/wifi",
                                              "hdmi0:audio = SD0:/sys/svc/hdmiaudio 0",
                                              "usb:02/06 = SD0:/sys/svc/usbnet", "usb:02/02/ff = SD0:/sys/svc/usbnet",
                                              "usb:01/01 = SD0:/sys/svc/usbaudio")
    # options.txt : options de desactivation en no_xxx (anciennement pas_xxx)
    $o = "$Drive\etc\options.txt"
    if (Test-Path $o) {
      $lignes = [IO.File]::ReadAllLines($o) | ForEach-Object { $_ -replace '^(\s*)pas_', '$1no_' }
      [IO.File]::WriteAllLines($o, [string[]]$lignes)
    }
    # wifi.txt : cles en anglais (reseau= -> ssid=, cle= -> key=, pays= -> country=)
    $w = "$Drive\etc\wifi.txt"
    if (Test-Path $w) {
      $lignes = [IO.File]::ReadAllLines($w) | ForEach-Object {
        $_ -replace '^(\s*#?\s*)reseau=', '$1ssid=' -replace '^(\s*#?\s*)cle=', '$1key=' -replace '^(\s*#?\s*)pays=', '$1country='
      }
      [IO.File]::WriteAllLines($w, [string[]]$lignes)
    }
    # sources et fichiers de test : copies exactes de src/ (refaites a neuf)
    foreach ($d in @("src","t")) {
      Remove-Item "$Drive\$d" -Recurse -Force -ErrorAction SilentlyContinue
    }
    # programmes d'une carte plus ancienne (<nom>.elf) : remplaces par les paquets
    foreach ($d in @("apps","bin","sys")) {
      Remove-Item "$Drive\$d\*.elf" -ErrorAction SilentlyContinue
    }
    # systeme de fichiers d'ARMPI : apps, polices... ; /etc : seulement les
    # fichiers absents (les reglages faits sur la carte, au panneau de
    # configuration, sont gardes)
    Get-ChildItem $sdroot | Where-Object { $_.Name -ne "etc" } |
      Copy-Item -Destination "$Drive\" -Recurse -Force
    foreach ($f in Get-ChildItem (Join-Path $sdroot "etc")) {
      if (-not (Test-Path "$Drive\etc\$($f.Name)")) { Copy-Item $f.FullName "$Drive\etc\" -Recurse }
    }
    # /etc/pkg : les fichiers absents, et la somme des kits de ce build
    New-Item -ItemType Directory -Force "$Drive\etc\pkg" | Out-Null
    foreach ($f in Get-ChildItem (Join-Path $sdroot "etc\pkg")) {
      if (($f.Name -eq "abi") -or -not (Test-Path "$Drive\etc\pkg\$($f.Name)")) { Copy-Item $f.FullName "$Drive\etc\pkg\" -Force }
    }
    Write-Host "Carte SD prete sur $Drive (premiere partition FAT32, ecran $Ecran)"
  }
}
