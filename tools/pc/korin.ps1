# Installe Korin OS pour PC sur la partition KORIN OS du disque interne (FAT32, nom de volume « KORIN OS »,
# partition de données de base, à côté de Windows) : le chargeur et son menu, le noyau, l'initrd, boot.cfg.
# RIEN sur la partition EFI de Windows : elle n'est jamais écrite, sauf par « cleanesp », qui en retire ce
# qu'une installation plus ancienne y avait mis (\EFI\Korin et \korin, rien d'autre).
# Voir docs/etude/PC-PILOTES.md, « Menu de démarrage ».
#
#   powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1 <action> [options]
#
#   install     copie \EFI\Korin\korin.efi, \korin\kernel.elf, \korin\initrd.tar sur KORIN OS
#   remove      les retire (refuse tant qu'une entrée « Korin OS » du firmware existe : undefault d'abord, ou -Force)
#   show        partition, fichiers, boot.cfg, ordre du firmware et entrées de Korin
#   config -Default korin|windows -Timeout S [-Cmdline "..."]   écrit \korin\boot.cfg ; config -Clear le retire
#   default     entrée « Korin OS » du firmware (\EFI\Korin\korin.efi SUR KORIN OS), mise en premier
#   undefault   Windows Boot Manager remis en premier, entrées « Korin OS » retirées
#   cleanesp    retire \EFI\Korin et \korin de la partition EFI de Windows (anciennes installations)
#
# Options : -Drive D: (la partition KORIN OS si son nom ne suffit pas), -Force, -DryRun (à blanc : rien n'est
# écrit, ni fichier, ni entrée du firmware ; les commandes qui changeraient quelque chose sont affichées ;
# « cleanesp » monte quand même la partition EFI le temps de la lire).
#
# Vérifications avant toute écriture : un seul volume « KORIN OS » avec une lettre, en FAT32, partition de type
# « Basic data » (jamais la partition EFI, jamais celle de Windows), sur le disque de Windows (le disque
# interne), pas sur un disque USB ou une carte.
# L'ordre de démarrage du firmware (NVRAM) ne change que par « default » et « undefault » ; l'entrée de
# Windows ({bootmgr}) n'est jamais supprimée. Secure Boot doit être désactivé (korin.efi n'est pas signé).
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('install', 'remove', 'show', 'config', 'default', 'undefault', 'cleanesp')]
    [string]$Action,
    [ValidateSet('korin', 'windows')][string]$Default,
    [ValidateRange(0, 3600)][int]$Timeout = -1,
    [string]$Cmdline,
    [string]$Drive,
    [switch]$Clear,
    [switch]$Force,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root 'build\pc\esp'
$srcEfi = Join-Path $build 'EFI\BOOT\BOOTX64.EFI'
$srcKernel = Join-Path $build 'korin\kernel.elf'
$srcInitrd = Join-Path $build 'korin\initrd.tar'
$label = 'KORIN OS'
$korinPath = '\EFI\Korin\korin.efi'
$typeBasic = '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}'    # Basic data (partition de données)
$typeEsp = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'      # EFI System
$bootmgrId = '{9dea862c-5cdd-4e70-acc1-f32b344d4795}'    # {bootmgr} : Windows Boot Manager
$fwbootmgrId = '{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}'  # {fwbootmgr} : l'ordre du firmware
$guidRx = '\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
# bcdedit et mountvol demandent une invite administrateur ; copier sur KORIN OS (FAT32), non
if (-not $admin -and -not $DryRun -and $Action -in 'remove', 'default', 'undefault', 'cleanesp') {
    throw "« $Action » : à lancer dans une invite PowerShell « en tant qu'administrateur »"
}
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
if ($DryRun) { Write-Host "*** À BLANC : rien ne sera écrit ***" }

# Une écriture : faite, ou seulement affichée avec -DryRun
function Invoke-Change([string]$what, [scriptblock]$do) {
    if ($DryRun) { Write-Host "  [à blanc] $what" } else { & $do }
}

# ---- la partition KORIN OS
function Find-Korin {
    $vols = @(Get-Volume | Where-Object { $_.FileSystemLabel -eq $label -and $_.DriveLetter })
    if ($Drive) {
        $want = $Drive.TrimEnd('\').TrimEnd(':').ToUpper()
        $vols = @($vols | Where-Object { [string]$_.DriveLetter -eq $want })
        if ($vols.Count -eq 0) { throw "$($want): n'est pas un volume « $label »" }
    }
    if ($vols.Count -eq 0) {
        throw "Aucun volume « $label » avec une lettre de lecteur (Gestion des disques : lui donner une lettre)"
    }
    if ($vols.Count -gt 1) {
        throw "Plusieurs volumes « $label » ($(($vols | ForEach-Object { "$($_.DriveLetter):" }) -join ', ')) : préciser -Drive X:"
    }
    $v = $vols[0]
    $L = "$($v.DriveLetter):"
    if ($v.FileSystem -ne 'FAT32') { throw "$L est en $($v.FileSystem) : la partition KORIN OS doit être en FAT32" }
    if ($L -eq $env:SystemDrive) { throw "$L est le volume de Windows : refusé" }
    $p = Get-Partition -DriveLetter $v.DriveLetter
    $type = ([string]$p.GptType).ToLower()
    if ($type -eq $typeEsp -or $p.IsSystem) { throw "$L est la partition système EFI : refusé (Korin va sur KORIN OS)" }
    if ($p.IsBoot) { throw "$L porte Windows (IsBoot) : refusé" }
    if ($type -ne $typeBasic) { throw "$L n'est pas une partition de données de base (type GPT $($p.GptType)) : refusé" }
    $sys = Get-Partition -DriveLetter $env:SystemDrive.TrimEnd(':')
    if ($p.DiskNumber -ne $sys.DiskNumber) {
        throw "$L n'est pas sur le disque de Windows (disque $($p.DiskNumber), Windows sur le disque $($sys.DiskNumber)) : refusé"
    }
    $disk = Get-Disk -Number $p.DiskNumber
    if ([string]$disk.BusType -match 'USB|SD|MMC|1394') { throw "$L est sur un disque $($disk.BusType) : refusé" }
    [pscustomobject]@{
        Letter = $L; Disk = $p.DiskNumber; Partition = $p.PartitionNumber
        Size = $v.Size; Free = $v.SizeRemaining; DiskName = $disk.FriendlyName; Bus = $disk.BusType
    }
}

# ---- entrées de démarrage du firmware, par bcdedit (/v : toujours des GUID ; les intitulés des champs
# peuvent être traduits selon la langue de Windows, on ne se fie donc qu'aux GUID et aux valeurs)
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
# Les entrées de Korin : chemin \EFI\Korin\korin.efi, jamais {bootmgr} ni une entrée qui mène à bootmgfw.efi
function Get-KorinEntries {
    Get-FwEntries | Where-Object {
        $_.Id -ne $bootmgrId -and $_.Id -ne $fwbootmgrId -and
        $_.Text -match [regex]::Escape($korinPath) -and $_.Text -notmatch 'bootmgfw\.efi'
    }
}
# Une entrée de Korin qui mène à la partition KORIN OS ($L) : « partition=D: » dans bcdedit /v
function Test-OnKorin($entry, [string]$L) {
    return [bool]$L -and $entry.Text -match ('partition=' + [regex]::Escape($L) + '(\s|$)')
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
function Show-KorinEntries([string]$L) {
    $k = @(Get-KorinEntries)
    if ($k.Count -eq 0) { Write-Host "Pas d'entrée « Korin OS » dans le firmware"; return }
    foreach ($e in $k) {
        $where = if (Test-OnKorin $e $L) { "sur KORIN OS ($L)" } else { 'PAS sur KORIN OS (ancienne entrée : « default » la remet sur KORIN OS)' }
        Write-Host "Entrée « $($e.Desc) » $($e.Id) : $where"
        $e.Text -split "`n" | Where-Object { $_ -match 'partition=|\\EFI\\' } | ForEach-Object { Write-Host "  | $($_.Trim())" }
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

# ---- cleanesp : la partition EFI de Windows, montée le temps du travail, en lecture sauf \EFI\Korin et \korin
function Invoke-CleanEsp {
    $kl = $null
    try { $kl = (Find-Korin).Letter } catch { Write-Host "Partition KORIN OS : $($_.Exception.Message)" }
    $old = @(Get-KorinEntries | Where-Object { -not (Test-OnKorin $_ $kl) })
    if ($old.Count -gt 0 -and -not $Force) {
        throw "Le firmware a encore une entrée « Korin OS » qui ne mène pas à KORIN OS ($($old.Id -join ', ')) : « korin.ps1 default » (la remet sur KORIN OS) ou « korin.ps1 undefault » d'abord (ou -Force)"
    }
    # déjà montée (mountvol X: /S lancé à la main) : on la reprend telle quelle et on la laisse montée
    $mounted = $false
    $already = (& mountvol) -match 'EFI System Partition is mounted at ([A-Z]):|partition syst.me EFI est mont.e (?:sur|en|.) ?([A-Z]):'
    $m = [regex]::Match(($already -join "`n"), '([A-Z]):\\?\s*$', 'Multiline')
    if ($m.Success) {
        $E = "$($m.Groups[1].Value):"
        Write-Host "Partition EFI déjà montée sur $E : reprise, laissée montée à la fin"
    } else {
        $used = (Get-PSDrive -PSProvider FileSystem).Name
        $letter = [char[]](83..90 + 71..82) | Where-Object { $used -notcontains [string]$_ } | Select-Object -First 1
        if (-not $letter) { throw "Aucune lettre de lecteur libre" }
        $E = "$($letter):"
        & mountvol $E /S
        if ($LASTEXITCODE -ne 0) { throw "mountvol $E /S a échoué (pas de partition EFI ?)" }
        $mounted = $true
    }
    try {
        # (Get-Volume ne voit pas une partition EFI montée par mountvol : DriveInfo, oui)
        $di = New-Object System.IO.DriveInfo $E
        if ($di.DriveFormat -notmatch '^FAT') { throw "$E est en $($di.DriveFormat) : ce n'est pas une partition EFI" }
        if (-not (Test-Path "$E\EFI\Microsoft\Boot\bootmgfw.efi")) {
            throw "$E n'a pas \EFI\Microsoft\Boot\bootmgfw.efi : ce n'est pas la partition EFI de Windows, rien n'est fait"
        }
        Write-Host "Partition EFI de Windows : $E, $([math]::Round($di.AvailableFreeSpace / 1MB)) Mo libres sur $([math]::Round($di.TotalSize / 1MB)) Mo"
        Get-ChildItem "$E\EFI" -Directory | ForEach-Object { Write-Host "  \EFI\$($_.Name)" }
        $found = $false
        foreach ($rel in '\EFI\Korin', '\korin') {
            $d = "$E$rel"
            if (-not (Test-Path $d)) { continue }
            $found = $true
            $files = @(Get-ChildItem $d -Recurse -Force)
            if ($files | Where-Object { $_.Name -match '^(bootmgfw\.efi|bootmgr\.efi|BCD)$' }) {
                throw "$rel contient un fichier de Windows (bootmgfw.efi, bootmgr.efi ou BCD) : rien n'est retiré"
            }
            $base = (Get-Item $d).FullName
            $files | ForEach-Object {
                Write-Host "  à retirer : $rel$($_.FullName.Substring($base.Length))  $(if (-not $_.PSIsContainer) { "$($_.Length) octets" })"
            }
            Invoke-Change "Remove-Item $d -Recurse" { Remove-Item $d -Recurse -Force }
            if (-not $DryRun) { Write-Host "Retiré de la partition EFI : $rel" }
        }
        if (-not $found) { Write-Host "Rien de Korin sur la partition EFI (\EFI\Korin et \korin absents)" }
    } finally {
        if ($mounted) { & mountvol $E /D | Out-Null }
    }
}

if ($Action -eq 'cleanesp') { Invoke-CleanEsp; return }
if ($Action -eq 'undefault') {
    Show-FwOrder "Ordre de démarrage AVANT"
    Invoke-Change "bcdedit /set {fwbootmgr} displayorder {bootmgr} /addfirst" {
        Invoke-Bcdedit /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst | Out-Null
    }
    foreach ($e in @(Get-KorinEntries)) {
        # jamais l'entrée de Windows (Get-KorinEntries l'exclut déjà : vérifié encore ici)
        if ($e.Id -eq $bootmgrId -or $e.Id -eq $fwbootmgrId -or $e.Text -match 'bootmgfw\.efi') { continue }
        $id = $e.Id
        Invoke-Change "bcdedit /delete $id" { Invoke-Bcdedit /delete $id | Out-Null }
        Write-Host "Entrée « Korin OS » retirée : $id"
    }
    if (-not $DryRun) { Show-FwOrder "Ordre de démarrage APRÈS" }
    Write-Host "Windows Boot Manager est de nouveau en premier ; les fichiers de Korin restent sur KORIN OS (F9, ou « remove »)."
    return
}

$K = Find-Korin
$L = $K.Letter
$dEfi = "$L\EFI\Korin"
$dKorin = "$L\korin"
$cfg = "$dKorin\boot.cfg"
$targets = @("$dEfi\korin.efi", "$dKorin\kernel.elf", "$dKorin\initrd.tar")
Write-Host "Partition KORIN OS : $L (disque $($K.Disk) « $($K.DiskName) », $($K.Bus), partition $($K.Partition)), $([math]::Round($K.Free / 1MB)) Mo libres sur $([math]::Round($K.Size / 1MB)) Mo"

switch ($Action) {
    'show' {
        foreach ($f in $targets + $cfg) {
            if (Test-Path $f) { Write-Host "  $($f.Substring(2))  $((Get-Item $f).Length) octets  $((Get-Item $f).LastWriteTime)" }
            else { Write-Host "  $($f.Substring(2))  absent" }
        }
        if (Test-Path $cfg) {
            Write-Host "\korin\boot.cfg :"
            Get-Content $cfg | ForEach-Object { Write-Host "  | $_" }
        } else {
            Write-Host "Pas de \korin\boot.cfg : menu, Windows par défaut, 5 s"
        }
        if ($admin -or $DryRun) {
            Show-FwOrder "Ordre de démarrage du firmware"
            Show-KorinEntries $L
        } else {
            Write-Host "(ordre du firmware et entrées de Korin : dans une invite administrateur)"
        }
    }
    'install' {
        $need = (Get-Item $srcEfi).Length + (Get-Item $srcKernel).Length + (Get-Item $srcInitrd).Length
        $have = 0
        foreach ($f in $targets) { if (Test-Path $f) { $have += (Get-Item $f).Length } }
        if ($K.Free + $have -lt $need + 4MB) {
            throw "Pas assez de place sur KORIN OS ($([math]::Round($K.Free / 1MB)) Mo libres, $([math]::Round($need / 1MB)) Mo nécessaires)"
        }
        Invoke-Change "New-Item $dEfi, $dKorin" {
            New-Item -ItemType Directory -Force $dEfi | Out-Null
            New-Item -ItemType Directory -Force $dKorin | Out-Null
        }
        Invoke-Change "Copy-Item $srcEfi -> $dEfi\korin.efi" { Copy-Item $srcEfi "$dEfi\korin.efi" -Force }
        Invoke-Change "Copy-Item $srcKernel -> $dKorin\kernel.elf" { Copy-Item $srcKernel "$dKorin\kernel.elf" -Force }
        Invoke-Change "Copy-Item $srcInitrd -> $dKorin\initrd.tar" { Copy-Item $srcInitrd "$dKorin\initrd.tar" -Force }
        Write-Host "Korin OS installé sur $L : \EFI\Korin\korin.efi, \korin\kernel.elf, \korin\initrd.tar"
        Write-Host "Au démarrage : Échap, F9, « Boot From EFI File », le volume KORIN OS, EFI, Korin, korin.efi"
        Write-Host "(ou « korin.ps1 default » pour démarrer sur le menu de Korin)"
    }
    'remove' {
        $k = @(Get-KorinEntries)
        if ($k.Count -gt 0 -and -not $Force) {
            throw "Le firmware a encore une entrée « Korin OS » ($($k.Id -join ', ')) : lancer d'abord « korin.ps1 undefault » (ou -Force)"
        }
        foreach ($f in $targets + $cfg) {
            if (Test-Path $f) { Invoke-Change "Remove-Item $f" { Remove-Item $f -Force }; Write-Host "Retiré : $($f.Substring(2))" }
        }
        # les dossiers, s'ils sont vides (KORIN OS garde tout le reste du système)
        foreach ($d in $dEfi, "$L\EFI", $dKorin) {
            if ((Test-Path $d) -and -not (Get-ChildItem $d -Force)) { Invoke-Change "Remove-Item $d" { Remove-Item $d -Force } }
        }
    }
    'config' {
        if ($Clear) {
            if (Test-Path $cfg) { Invoke-Change "Remove-Item $cfg" { Remove-Item $cfg -Force } }
            Write-Host "\korin\boot.cfg retiré : menu, Windows par défaut, 5 s"
            break
        }
        if (-not (Test-Path $dKorin)) { throw "Korin n'est pas installé ($dKorin absent) : « korin.ps1 install » d'abord" }
        $lines = @('# Menu de demarrage de Korin OS (tools\pc\korin.ps1 config)')  # (fichier en ASCII)
        if ($Default) { $lines += "default = $Default" }
        if ($Timeout -ge 0) { $lines += "timeout = $Timeout" }
        if ($PSBoundParameters.ContainsKey('Cmdline')) { $lines += "cmdline = $Cmdline" }
        $text = ($lines -join "`n") + "`n"
        Invoke-Change "écrire $cfg" { Set-Content -LiteralPath $cfg -Value $text -Encoding Ascii -NoNewline }
        Write-Host "\korin\boot.cfg :"
        $lines | ForEach-Object { Write-Host "  | $_" }
    }
    'default' {
        if (-not (Test-Path "$dEfi\korin.efi")) { throw "$L\EFI\Korin\korin.efi absent : « korin.ps1 install » d'abord" }
        Assert-BootReady
        Show-FwOrder "Ordre de démarrage AVANT"
        $k = @(Get-KorinEntries)
        if ($k.Count -gt 0) {
            $id = $k[0].Id
            Write-Host "Entrée « Korin OS » déjà là : $id (remise sur $L)"
        } else {
            # une copie de {bootmgr} : une nouvelle entrée du firmware, puis sa partition et son chemin
            if ($DryRun) {
                Write-Host "  [à blanc] bcdedit /copy {bootmgr} /d ""Korin OS"""
                $id = '{nouvelle-entree}'
            } else {
                $out = Invoke-Bcdedit /copy '{bootmgr}' /d 'Korin OS'
                $m = [regex]::Match(($out -join ' '), $guidRx)
                if (-not $m.Success) { throw "bcdedit /copy n'a pas donné d'identifiant : $($out -join ' ')" }
                $id = $m.Value.ToLower()
                if ($id -eq $bootmgrId -or $id -eq $fwbootmgrId) { throw "identifiant inattendu ($id) : rien n'est changé" }
            }
        }
        Invoke-Change "bcdedit /set $id device partition=$L" { Invoke-Bcdedit /set $id device "partition=$L" | Out-Null }
        Invoke-Change "bcdedit /set $id path $korinPath" { Invoke-Bcdedit /set $id path $korinPath | Out-Null }
        Invoke-Change "bcdedit /set {fwbootmgr} displayorder $id /addfirst" {
            Invoke-Bcdedit /set '{fwbootmgr}' displayorder $id /addfirst | Out-Null
        }
        if (-not $DryRun) {
            Write-Host "Entrée « Korin OS » : $id -> partition=$L $korinPath"
            Show-FwOrder "Ordre de démarrage APRÈS"
            Show-KorinEntries $L
        }
        Write-Host "Le PC démarrera sur le menu de Korin (Windows y reste ; « korin.ps1 undefault » pour revenir)."
    }
}
