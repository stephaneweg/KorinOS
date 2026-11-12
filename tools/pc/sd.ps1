# Copie Korin OS pour PC (build/pc/esp) sur une carte SD ou une clé USB déjà
# formatée en FAT32, pour démarrer le PC en UEFI :
#   powershell -File tools\pc\sd.ps1 E:
# Ne formate rien. Refuse un disque qui n'est pas amovible ou pas en FAT, et
# garde une copie d'un BOOTX64.EFI qui ne serait pas celui de Korin.
param(
    [Parameter(Mandatory = $true)][string]$Drive,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$esp = Join-Path $root 'build\pc\esp'
$efi = Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'
$kernel = Join-Path $esp 'korin\kernel.elf'
if (-not (Test-Path $efi) -or -not (Test-Path $kernel)) {
    throw "Construire d'abord : wsl sh tools/pc/build.sh"
}

$Drive = $Drive.TrimEnd('\').TrimEnd(':') + ':'
$disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$Drive'"
if (-not $disk) { throw "Lecteur $Drive introuvable" }
if ($disk.DeviceID -eq $env:SystemDrive) { throw "$Drive est le disque de Windows : refusé" }
if ($disk.DriveType -ne 2 -and -not $Force) {
    throw "$Drive n'est pas un disque amovible (type $($disk.DriveType)) : refusé (-Force pour passer outre)"
}
if ($disk.FileSystem -notmatch '^FAT') {
    throw "$Drive est en $($disk.FileSystem) : l'UEFI ne démarre que sur du FAT (FAT32)"
}

$dstBoot = "$Drive\EFI\BOOT"
$dstKorin = "$Drive\korin"
New-Item -ItemType Directory -Force $dstBoot | Out-Null
New-Item -ItemType Directory -Force $dstKorin | Out-Null

$dstEfi = Join-Path $dstBoot 'BOOTX64.EFI'
if (Test-Path $dstEfi) {
    $old = [IO.File]::ReadAllBytes($dstEfi)
    $text = [Text.Encoding]::Unicode.GetString($old)
    if ($text -notmatch 'Korin OS') {
        Copy-Item $dstEfi "$dstEfi.avant-korin" -Force
        Write-Host "Ancien BOOTX64.EFI gardé sous BOOTX64.EFI.avant-korin"
    }
}
Copy-Item $efi $dstEfi -Force
Copy-Item $kernel (Join-Path $dstKorin 'kernel.elf') -Force
# l'initrd : les premiers programmes (moniteur, pcid, pilotes, essais) ; sans lui, le noyau démarre seul
$initrd = Join-Path $esp 'korin\initrd.tar'
if (Test-Path $initrd) { Copy-Item $initrd (Join-Path $dstKorin 'initrd.tar') -Force }
Write-Host "Korin OS copié sur $Drive ($($disk.VolumeName)) : EFI\BOOT\BOOTX64.EFI, korin\kernel.elf et korin\initrd.tar"
