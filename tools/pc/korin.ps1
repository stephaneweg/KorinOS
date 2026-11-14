# Installe Korin OS pour PC sur la partition KORIN OS du disque interne (FAT32, nom de volume « KORIN OS »,
# partition de données de base, à côté de Windows) : le chargeur et son menu, le noyau, l'initrd, boot.cfg.
# RIEN sur la partition EFI de Windows : elle n'est jamais écrite, sauf par « cleanesp », qui en retire ce
# qu'une installation plus ancienne y avait mis (\EFI\Korin et \korin, rien d'autre).
# Voir docs/etude/PC-PILOTES.md, « Menu de démarrage ».
#
#   powershell -ExecutionPolicy Bypass -File tools\pc\korin.ps1 <action> [options]
#
#   install     copie \EFI\Korin\korin.efi, \korin\kernel.elf, \korin\initrd.tar sur KORIN OS, puis l'arbre
#               système (build\pc\sysroot de « wsl sh tools/pc/sysroot.sh » : \bin, \sys, \lib, \etc, \res, et
#               \home vide) ; la liste de ce qui est installé est gardée dans \korin\installed.txt : une
#               nouvelle installation retire seulement les fichiers de cette liste qui ne sont plus dans
#               l'arbre, rien d'autre ; \etc\options.txt (vos réglages) n'est écrit que s'il manque, \home
#               jamais ; -NoSystem : le chargeur, le noyau et l'initrd seulement
#   remove      les retire, avec les fichiers de \korin\installed.txt (sauf \etc\options.txt et \home) ;
#               refuse tant qu'une entrée « Korin OS » du firmware existe : undefault d'abord, ou -Force
#   show        partition, fichiers, boot.cfg, arbre système installé, ordre du firmware et entrées de Korin
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
    [switch]$NoSystem,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root 'build\pc\esp'
$srcEfi = Join-Path $build 'EFI\BOOT\BOOTX64.EFI'
$srcKernel = Join-Path $build 'korin\kernel.elf'
$srcInitrd = Join-Path $build 'korin\initrd.tar'
$sysroot = Join-Path $root 'build\pc\sysroot'
$sysTop = @('bin', 'sys', 'lib', 'etc', 'res', 'apps', 'fonts', 'gcc', 'python', '.vibe', '.claude-acp', 'dotnet', 'var')  # les dossiers de l'arbre système (\home : jamais) ; python : CPython ; dotnet : .NET (tools/pc/sysroot_dotnet.sh) ; .claude-acp : de même pour claude-acp (.env, config.toml, sessions : jamais touchés) ; .vibe : AGENTS.md et compétences de Vibe (les comptes de l'utilisateur, .vibe\.env et config.toml, ne sont pas dans l'arbre : jamais touchés) ; var : seulement var\pkg\db, la base des paquets de cet arbre (ARCH=x86_64 tools/pkg/mkrepo.py --db : pkg et Logiciels savent ce qui est installé)
$sysKeep = @('etc\options.txt', 'etc\wifi.txt', 'etc\pointer.ini')  # écrits seulement s'ils manquent, jamais retirés (vos réglages, vos clés Wi-Fi)
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
    if (-not $NoSystem -and -not (Test-Path (Join-Path $sysroot 'sys\svc\init2'))) {
        throw "Arbre système absent ou incomplet : $sysroot (« wsl sh tools/pc/sysroot.sh » après build.sh ; ou -NoSystem)"
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

# ---- l'arbre système (build\pc\sysroot) ; \korin\installed.txt : les fichiers qu'install y a copiés (chemins
# relatifs à la racine de KORIN OS), seuls candidats au retrait (remove, ou plus dans l'arbre)
function Get-SysFiles {
    $base = (Get-Item -LiteralPath $sysroot).FullName.TrimEnd('\')
    # \home de l'arbre (exemples des apps, PC_APPS=1) : jamais copié, c'est SD1: (vos fichiers)
    @(Get-ChildItem -LiteralPath $sysroot -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($base.Length + 1) } |
        Where-Object { ($_ -split '\\')[0] -ne 'home' } | Sort-Object)
}
# un chemin de la liste : relatif, sans « .. », dans un dossier de l'arbre (jamais \home, \EFI, \korin)
function Test-SysPath([string]$rel) {
    if (-not $rel -or $rel -match '(^|\\)\.\.(\\|$)' -or $rel -match '^[\\/]' -or $rel -match ':') { return $false }
    return $sysTop -contains ($rel -split '\\')[0]
}
function Read-Manifest([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    @(Get-Content -LiteralPath $path | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') -and (Test-SysPath $_) })
}
# les dossiers de ces fichiers (et leurs parents dans l'arbre), retirés s'ils sont vides
function Remove-EmptyParents([string]$L, [string[]]$rels) {
    $dirs = @{}
    foreach ($rel in $rels) {
        $p = Split-Path -Parent $rel
        while ($p) { $dirs[$p] = 1; $p = Split-Path -Parent $p }
    }
    foreach ($d in ($dirs.Keys | Sort-Object { $_.Length } -Descending)) {
        $full = Join-Path $L $d
        if ((Test-Path -LiteralPath $full) -and -not (Get-ChildItem -LiteralPath $full -Force)) {
            Invoke-Change "Remove-Item $full" { Remove-Item -LiteralPath $full -Force }
        }
    }
}
# octets à écrire et octets déjà là (remplacés) pour l'arbre
function Measure-System([string]$L, [string[]]$files) {
    $need = 0; $have = 0
    foreach ($rel in $files) {
        $dst = Join-Path $L $rel
        $there = Test-Path -LiteralPath $dst
        if ($sysKeep -contains $rel -and $there) { continue }
        $need += (Get-Item -LiteralPath (Join-Path $sysroot $rel)).Length
        if ($there) { $have += (Get-Item -LiteralPath $dst).Length }
    }
    [pscustomobject]@{ Need = $need; Have = $have }
}
function Install-System([string]$L, [string]$manifest, [string[]]$files) {
    $old = @(Read-Manifest $manifest)
    $count = @{}
    $kept = @()
    foreach ($rel in $files) {
        $src = Join-Path $sysroot $rel
        $dst = Join-Path $L $rel
        if ($sysKeep -contains $rel -and (Test-Path -LiteralPath $dst)) { $kept += $rel; continue }
        $top = ($rel -split '\\')[0]
        $count[$top] = 1 + [int]$count[$top]
        if ($DryRun) { continue }
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
        Copy-Item -LiteralPath $src $dst -Force
    }
    foreach ($top in $sysTop) {
        if ($count[$top]) {
            $verb = if ($DryRun) { '  [à blanc] copier' } else { '  copié :' }
            Write-Host "$verb $($count[$top]) fichier(s) dans \$top"
        }
    }
    foreach ($rel in $kept) { Write-Host "  gardé (vos réglages) : \$rel" }
    # ce qu'une installation d'avant avait mis et qui n'est plus dans l'arbre
    $gone = @($old | Where-Object { $files -notcontains $_ -and $sysKeep -notcontains $_ })
    foreach ($rel in $gone) {
        $f = Join-Path $L $rel
        if (Test-Path -LiteralPath $f -PathType Leaf) {
            Invoke-Change "Remove-Item $f" { Remove-Item -LiteralPath $f -Force }
            Write-Host "  retiré (plus dans l'arbre) : \$rel"
        }
    }
    if ($gone.Count -and -not $DryRun) { Remove-EmptyParents $L $gone }
    if (-not (Test-Path -LiteralPath "$L\home")) {
        Invoke-Change "New-Item $L\home" { New-Item -ItemType Directory "$L\home" | Out-Null }
    }
    $text = "# Fichiers de l'arbre systeme installes par tools\pc\korin.ps1 (remove les retire ; \home jamais)`n" +
        (($files | ForEach-Object { $_ }) -join "`n") + "`n"
    Invoke-Change "écrire $manifest ($($files.Count) fichiers)" {
        Set-Content -LiteralPath $manifest -Value $text -Encoding Ascii -NoNewline
    }
}

$K = Find-Korin
$L = $K.Letter
$dEfi = "$L\EFI\Korin"
$dKorin = "$L\korin"
$cfg = "$dKorin\boot.cfg"
$manifest = "$dKorin\installed.txt"
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
        $m = @(Read-Manifest $manifest)
        if ($m.Count) {
            $missing = @($m | Where-Object { -not (Test-Path -LiteralPath (Join-Path $L $_)) })
            $tops = ($sysTop | ForEach-Object { $t = $_; "\$t $(@($m | Where-Object { ($_ -split '\\')[0] -eq $t }).Count)" }) -join ', '
            Write-Host "Arbre système : $($m.Count) fichiers installés (\korin\installed.txt : $tops), $($missing.Count) absent(s)"
            $missing | Select-Object -First 10 | ForEach-Object { Write-Host "  absent : \$_" }
        } else {
            Write-Host "Arbre système : pas installé (pas de \korin\installed.txt)"
        }
        Write-Host "\home (SD1:) : $(if (Test-Path -LiteralPath "$L\home") { 'présent' } else { 'absent' })"
        if (Test-Path -LiteralPath "$L\etc\options.txt") {
            $opts = @(Get-Content -LiteralPath "$L\etc\options.txt" | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') })
            Write-Host "\etc\options.txt : $(if ($opts.Count) { $opts -join ' ; ' } else { '(rien d''actif)' })"
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
        $sysFiles = @()
        if (-not $NoSystem) {
            $sysFiles = @(Get-SysFiles)
            $bad = @($sysFiles | Where-Object { -not (Test-SysPath $_) })
            if ($bad.Count) { throw "L'arbre système a des fichiers hors de \$($sysTop -join ', \') : $($bad -join ', ') (rien n'est écrit)" }
            $sm = Measure-System $L $sysFiles
            $need += $sm.Need
            $have += $sm.Have
        }
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
        if (-not $NoSystem) {
            Write-Host "Arbre système ($sysroot, $($sysFiles.Count) fichiers) :"
            Install-System $L $manifest $sysFiles
            if (-not (Test-Path -LiteralPath "$L\etc\wifi.txt") -and (Test-Path -LiteralPath "$L\etc\wifi.example.txt")) {
                Write-Host "Wi-Fi : pas de \etc\wifi.txt ; copiez \etc\wifi.example.txt en \etc\wifi.txt et mettez-y vos réseaux (ou réglez-les dans Korin : Réglages > Wi-Fi)"
            }
        }
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
        # l'arbre système : seulement les fichiers de \korin\installed.txt (sauf vos réglages), puis leurs
        # dossiers vides ; \home et le reste de KORIN OS ne bougent pas
        $m = @(Read-Manifest $manifest)
        if ($m.Count) {
            $del = @($m | Where-Object { $sysKeep -notcontains $_ })
            $n = 0
            foreach ($rel in $del) {
                $f = Join-Path $L $rel
                if (Test-Path -LiteralPath $f -PathType Leaf) { Invoke-Change "Remove-Item $f" { Remove-Item -LiteralPath $f -Force }; $n++ }
            }
            Write-Host "Arbre système : $n fichier(s) retiré(s)$(if ($DryRun) { ' (à blanc)' })"
            foreach ($rel in @($m | Where-Object { $sysKeep -contains $_ })) {
                if (Test-Path -LiteralPath (Join-Path $L $rel)) { Write-Host "  gardé (vos réglages) : \$rel" }
            }
            if (-not $DryRun) { Remove-EmptyParents $L $del }
            Invoke-Change "Remove-Item $manifest" { Remove-Item -LiteralPath $manifest -Force }
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
