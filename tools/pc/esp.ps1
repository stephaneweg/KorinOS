# Installe Korin OS pour PC sur la partition système EFI du disque interne, à côté de Windows, sans toucher
# à Windows : le PC démarre toujours Windows par défaut ; Korin se lance au menu de démarrage du firmware
# (HP : Échap puis F9, « Boot From EFI File », la partition EFI, EFI\Korin\korin.efi).
#
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 install     (invite administrateur)
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 remove      (retire les fichiers de Korin)
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 show        (montre ce qui est installé)
#
# Ce qui est écrit (et rien d'autre) :
#   \EFI\Korin\korin.efi    le chargeur (build\pc\esp\EFI\BOOT\BOOTX64.EFI)
#   \korin\kernel.elf       le noyau
#   \korin\initrd.tar       les premiers programmes
# Jamais touchés : \EFI\Microsoft, \EFI\Boot, l'ordre de démarrage du firmware (NVRAM), le BCD de Windows.
# La partition est montée sous une lettre libre le temps de la copie, puis démontée.
# Secure Boot doit être désactivé pour lancer korin.efi (non signé).
param(
    [Parameter(Mandatory = $true)][ValidateSet('install', 'remove', 'show')][string]$Action
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$esp = Join-Path $root 'build\pc\esp'
$srcEfi = Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'
$srcKernel = Join-Path $esp 'korin\kernel.elf'
$srcInitrd = Join-Path $esp 'korin\initrd.tar'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw "À lancer dans une invite PowerShell « en tant qu'administrateur »" }
if ($Action -eq 'install') {
    foreach ($f in $srcEfi, $srcKernel, $srcInitrd) {
        if (-not (Test-Path $f)) { throw "Absent : $f (construire d'abord : wsl sh tools/pc/build.sh)" }
    }
}

# une lettre libre
$used = (Get-PSDrive -PSProvider FileSystem).Name
$letter = [char[]](83..90 + 71..82) | Where-Object { $used -notcontains [string]$_ } | Select-Object -First 1
if (-not $letter) { throw "Aucune lettre de lecteur libre" }
$L = "$($letter):"

& mountvol $L /S
if ($LASTEXITCODE -ne 0) { throw "mountvol $L /S a échoué (pas de partition EFI ?)" }
try {
    # vérifier que c'est bien la partition EFI de Windows
    # (Get-Volume ne voit pas une partition EFI montée par mountvol : DriveInfo, oui)
    $di = New-Object System.IO.DriveInfo $L
    $vol = [pscustomobject]@{ FileSystem = $di.DriveFormat; SizeRemaining = $di.AvailableFreeSpace; Size = $di.TotalSize }
    if ($vol.FileSystem -notmatch '^FAT') { throw "$L est en $($vol.FileSystem) : ce n'est pas une partition EFI" }
    if (-not (Test-Path "$L\EFI\Microsoft\Boot\bootmgfw.efi")) {
        throw "$L n'a pas \EFI\Microsoft\Boot\bootmgfw.efi : ce n'est pas la partition EFI de Windows, rien n'est fait"
    }
    $dKorinEfi = "$L\EFI\Korin"
    $dKorin = "$L\korin"
    switch ($Action) {
        'show' {
            Write-Host "Partition EFI : $L, $([math]::Round($vol.SizeRemaining / 1MB)) Mo libres sur $([math]::Round($vol.Size / 1MB)) Mo"
            Get-ChildItem "$L\EFI" -Directory | ForEach-Object { Write-Host "  \EFI\$($_.Name)" }
            foreach ($d in $dKorinEfi, $dKorin) {
                if (Test-Path $d) { Get-ChildItem $d | ForEach-Object { Write-Host "  $($_.FullName.Substring(2))  $($_.Length) octets" } }
            }
        }
        'install' {
            $need = (Get-Item $srcEfi).Length + (Get-Item $srcKernel).Length + (Get-Item $srcInitrd).Length
            $have = 0
            foreach ($f in "$dKorinEfi\korin.efi", "$dKorin\kernel.elf", "$dKorin\initrd.tar") {
                if (Test-Path $f) { $have += (Get-Item $f).Length }
            }
            if ($vol.SizeRemaining + $have -lt $need + 4MB) {
                throw "Pas assez de place sur la partition EFI ($([math]::Round($vol.SizeRemaining / 1MB)) Mo libres, $([math]::Round($need / 1MB)) Mo nécessaires)"
            }
            New-Item -ItemType Directory -Force $dKorinEfi | Out-Null
            New-Item -ItemType Directory -Force $dKorin | Out-Null
            Copy-Item $srcEfi "$dKorinEfi\korin.efi" -Force
            Copy-Item $srcKernel "$dKorin\kernel.elf" -Force
            Copy-Item $srcInitrd "$dKorin\initrd.tar" -Force
            Write-Host "Korin OS installé sur la partition EFI : \EFI\Korin\korin.efi, \korin\kernel.elf, \korin\initrd.tar"
            Write-Host "Au démarrage : Échap, F9, « Boot From EFI File », la partition EFI, EFI, Korin, korin.efi"
        }
        'remove' {
            foreach ($d in $dKorinEfi, $dKorin) {
                if (Test-Path $d) { Remove-Item $d -Recurse -Force; Write-Host "Retiré : $($d.Substring(2))" }
            }
        }
    }
} finally {
    & mountvol $L /D | Out-Null
}
