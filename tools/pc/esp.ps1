# Installe Korin OS pour PC sur la partition système EFI du disque interne, à côté de Windows, sans toucher
# à Windows. Le chargeur de Korin a son menu de démarrage (Korin, Windows, réglages du firmware, redémarrer,
# éteindre ; docs/etude/PC-PILOTES.md, « Menu de démarrage »).
#
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 install     (invite administrateur)
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 remove      (retire les fichiers de Korin)
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 show        (fichiers, boot.cfg, ordre du firmware)
#   ... esp.ps1 config -Default korin -Timeout 5 [-Cmdline "..."]          (écrit \korin\boot.cfg)
#   ... esp.ps1 config -Clear                                             (retire \korin\boot.cfg : défauts)
#   ... esp.ps1 default      entrée « Korin OS » du firmware, mise en premier : le PC démarre sur le menu de Korin
#   ... esp.ps1 undefault    Windows Boot Manager remis en premier, entrée « Korin OS » retirée
#
# Fichiers écrits (install, config ; rien d'autre) :
#   \EFI\Korin\korin.efi    le chargeur (build\pc\esp\EFI\BOOT\BOOTX64.EFI)
#   \korin\kernel.elf       le noyau
#   \korin\initrd.tar       les premiers programmes
#   \korin\boot.cfg         seulement par « config » (sans lui : menu, Windows par défaut, 5 s)
# Jamais touchés : \EFI\Microsoft, \EFI\Boot, le BCD de Windows (son entrée {bootmgr}).
# L'ordre de démarrage du firmware (NVRAM) ne change que par « default » et « undefault », lancés à la main ;
# l'entrée de Windows n'est jamais supprimée.
# Sans « default » : le PC démarre Windows comme avant ; Korin se lance au menu de démarrage du firmware
# (HP : Échap puis F9, « Boot From EFI File », la partition EFI, EFI\Korin\korin.efi).
# La partition est montée sous une lettre libre le temps du travail, puis démontée.
# Secure Boot doit être désactivé pour lancer korin.efi (non signé).
param(
    [Parameter(Mandatory = $true)][ValidateSet('install', 'remove', 'show', 'config', 'default', 'undefault')]
    [string]$Action,
    [ValidateSet('korin', 'windows')][string]$Default,
    [ValidateRange(0, 3600)][int]$Timeout = -1,
    [string]$Cmdline,
    [switch]$Clear,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$esp = Join-Path $root 'build\pc\esp'
$srcEfi = Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'
$srcKernel = Join-Path $esp 'korin\kernel.elf'
$srcInitrd = Join-Path $esp 'korin\initrd.tar'
$korinPath = '\EFI\Korin\korin.efi'
$bootmgrId = '{9dea862c-5cdd-4e70-acc1-f32b344d4795}'    # {bootmgr} : Windows Boot Manager
$fwbootmgrId = '{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}'  # {fwbootmgr} : l'ordre du firmware
$guidRx = '\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw "À lancer dans une invite PowerShell « en tant qu'administrateur »" }
if ($Action -eq 'install') {
    foreach ($f in $srcEfi, $srcKernel, $srcInitrd) {
        if (-not (Test-Path $f)) { throw "Absent : $f (construire d'abord : wsl sh tools/pc/build.sh)" }
    }
}
if ($Action -eq 'config' -and -not $Clear -and -not $Default -and $Timeout -lt 0 -and -not $PSBoundParameters.ContainsKey('Cmdline')) {
    throw "config : donner -Default korin|windows, -Timeout secondes, -Cmdline ""..."" (ou -Clear)"
}
if ($PSBoundParameters.ContainsKey('Cmdline') -and ($Cmdline.Length -gt 127 -or $Cmdline -match '[^\x20-\x7E]')) {
    throw "-Cmdline : 127 caractères ASCII au plus, sur une ligne"
}

# ---- entrées de démarrage du firmware, par bcdedit (/v : toujours des GUID ; les intitulés des champs sont
# traduits selon la langue de Windows, on ne se fie donc qu'aux GUID et aux chemins)
function Invoke-Bcdedit {
    $out = & bcdedit.exe @args  # (sans 2>&1 : PowerShell 5.1 en ferait une erreur fatale)
    if ($LASTEXITCODE -ne 0) { throw "bcdedit $($args -join ' ') a échoué : $($out -join ' ')" }
    return $out
}
function Get-FwEntries {
    $text = (Invoke-Bcdedit /enum firmware /v) -join "`n"
    foreach ($b in ($text -split "`n\s*`n")) {
        $g = [regex]::Match($b, $guidRx)
        if (-not $g.Success) { continue }
        $desc = [regex]::Match($b, '(?m)^description\s+(.+)$')
        [pscustomobject]@{
            Id   = $g.Value.ToLower()
            Desc = $(if ($desc.Success) { $desc.Groups[1].Value.Trim() } else { '' })
            Text = $b
        }
    }
}
function Get-KorinEntries {
    Get-FwEntries | Where-Object {
        $_.Id -ne $bootmgrId -and $_.Id -ne $fwbootmgrId -and
        $_.Text -match [regex]::Escape($korinPath) -and $_.Text -notmatch 'bootmgfw\.efi'
    }
}
function Show-FwOrder([string]$title) {
    Write-Host "$title :"
    $entries = @(Get-FwEntries)
    $fw = $entries | Where-Object { $_.Id -eq $fwbootmgrId } | Select-Object -First 1
    if (-not $fw) { Write-Host '  ({fwbootmgr} introuvable)'; return }
    $ids = [regex]::Matches($fw.Text, $guidRx) | ForEach-Object { $_.Value.ToLower() } | Select-Object -Skip 1
    $i = 1
    foreach ($id in $ids) {
        $e = $entries | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        $name = if ($id -eq $bootmgrId) { 'Windows Boot Manager {bootmgr}' } elseif ($e -and $e.Desc) { $e.Desc } else { '' }
        $mark = if ($e -and $e.Text -match [regex]::Escape($korinPath)) { '   <- Korin' } else { '' }
        Write-Host ("  {0,2}. {1}  {2}{3}" -f $i, $id, $name, $mark)
        $i++
    }
}
function Assert-BootReady {
    # korin.efi n'est pas signé : avec Secure Boot, le firmware le refuserait
    $sb = $null
    try { $sb = Confirm-SecureBootUEFI } catch { }
    if ($sb -eq $true -and -not $Force) {
        throw "Secure Boot est actif : korin.efi serait refusé. Le désactiver dans les réglages du firmware d'abord (ou -Force)"
    }
    # BitLocker mesure la chaîne de démarrage (TPM) : démarrer Windows par korin.efi la change et Windows
    # demanderait la clé de récupération
    $bl = $null
    try { $bl = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop }
    catch { Write-Host "BitLocker : état inconnu ($($_.Exception.Message))" }
    if ($bl -and $bl.ProtectionStatus -eq 'On' -and -not $Force) {
        throw "BitLocker protège $($env:SystemDrive) : finir le déchiffrement (ou suspendre la protection) d'abord, sinon Windows demandera la clé de récupération (ou -Force)"
    }
    if ($bl -and $bl.VolumeStatus -ne 'FullyDecrypted') {
        Write-Host "Attention : $($env:SystemDrive) est « $($bl.VolumeStatus) » (BitLocker)."
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
    $cfg = "$dKorin\boot.cfg"
    switch ($Action) {
        'show' {
            Write-Host "Partition EFI : $L, $([math]::Round($vol.SizeRemaining / 1MB)) Mo libres sur $([math]::Round($vol.Size / 1MB)) Mo"
            Get-ChildItem "$L\EFI" -Directory | ForEach-Object { Write-Host "  \EFI\$($_.Name)" }
            foreach ($d in $dKorinEfi, $dKorin) {
                if (Test-Path $d) { Get-ChildItem $d | ForEach-Object { Write-Host "  $($_.FullName.Substring(2))  $($_.Length) octets" } }
            }
            if (Test-Path $cfg) {
                Write-Host "\korin\boot.cfg :"
                Get-Content $cfg | ForEach-Object { Write-Host "  | $_" }
            } else {
                Write-Host "Pas de \korin\boot.cfg : menu, Windows par défaut, 5 s"
            }
            Show-FwOrder "Ordre de démarrage du firmware"
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
            Write-Host "(ou « esp.ps1 default » pour démarrer sur le menu de Korin)"
        }
        'remove' {
            $k = @(Get-KorinEntries)
            if ($k.Count -gt 0 -and -not $Force) {
                throw "Le firmware a encore une entrée « Korin OS » ($($k.Id -join ', ')) : lancer d'abord « esp.ps1 undefault » (ou -Force)"
            }
            foreach ($d in $dKorinEfi, $dKorin) {
                if (Test-Path $d) { Remove-Item $d -Recurse -Force; Write-Host "Retiré : $($d.Substring(2))" }
            }
        }
        'config' {
            if ($Clear) {
                if (Test-Path $cfg) { Remove-Item $cfg -Force }
                Write-Host "\korin\boot.cfg retiré : menu, Windows par défaut, 5 s"
                break
            }
            if (-not (Test-Path $dKorin)) { throw "Korin n'est pas installé ($dKorin absent) : « esp.ps1 install » d'abord" }
            $lines = @('# Menu de démarrage de Korin OS (tools\pc\esp.ps1 config)')
            if ($Default) { $lines += "default = $Default" }
            if ($Timeout -ge 0) { $lines += "timeout = $Timeout" }
            if ($PSBoundParameters.ContainsKey('Cmdline')) { $lines += "cmdline = $Cmdline" }
            [IO.File]::WriteAllText($cfg, (($lines -join "`n") + "`n"), [Text.Encoding]::ASCII)
            Write-Host "\korin\boot.cfg :"
            $lines | ForEach-Object { Write-Host "  | $_" }
        }
        'default' {
            if (-not (Test-Path "$dKorinEfi\korin.efi")) { throw "\EFI\Korin\korin.efi absent : « esp.ps1 install » d'abord" }
            Assert-BootReady
            Show-FwOrder "Ordre de démarrage AVANT"
            $k = @(Get-KorinEntries)
            if ($k.Count -gt 0) {
                $id = $k[0].Id
                Write-Host "Entrée « Korin OS » déjà là : $id"
            } else {
                # une copie de {bootmgr} : une nouvelle entrée du firmware sur la même partition, puis son chemin
                $out = Invoke-Bcdedit /copy '{bootmgr}' /d 'Korin OS'
                $m = [regex]::Match(($out -join ' '), $guidRx)
                if (-not $m.Success) { throw "bcdedit /copy n'a pas donné d'identifiant : $($out -join ' ')" }
                $id = $m.Value.ToLower()
                if ($id -eq $bootmgrId) { throw "identifiant inattendu ($id) : rien n'est changé" }
                Invoke-Bcdedit /set $id path $korinPath | Out-Null
                Write-Host "Entrée « Korin OS » créée : $id -> $korinPath"
            }
            Invoke-Bcdedit /set '{fwbootmgr}' displayorder $id /addfirst | Out-Null
            Show-FwOrder "Ordre de démarrage APRÈS"
            Write-Host "Le PC démarrera sur le menu de Korin (Windows y reste ; « esp.ps1 undefault » pour revenir)."
        }
        'undefault' {
            Show-FwOrder "Ordre de démarrage AVANT"
            Invoke-Bcdedit /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst | Out-Null
            foreach ($e in @(Get-KorinEntries)) {
                # jamais l'entrée de Windows (Get-KorinEntries l'exclut déjà : vérifié encore ici)
                if ($e.Id -eq $bootmgrId -or $e.Text -match 'bootmgfw\.efi') { continue }
                Invoke-Bcdedit /delete $e.Id | Out-Null
                Write-Host "Entrée « Korin OS » retirée : $($e.Id)"
            }
            Show-FwOrder "Ordre de démarrage APRÈS"
            Write-Host "Windows Boot Manager est de nouveau en premier ; les fichiers de Korin restent (F9, ou « remove »)."
        }
    }
} finally {
    & mountvol $L /D | Out-Null
}
