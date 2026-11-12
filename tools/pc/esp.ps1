# ANCIEN : Korin OS sur la partition système EFI de Windows. Depuis le 12/11, TOUT Korin va sur la partition
# KORIN OS (tools\pc\korin.ps1 : install, show, config, default, undefault, et cleanesp pour retirer de la
# partition EFI ce qu'une ancienne installation y avait mis). Voir docs/etude/PC-PILOTES.md, « Menu de démarrage ».
#
# Ce qui reste ici :
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 show      (lecture : fichiers, boot.cfg, ordre du firmware)
#   powershell -ExecutionPolicy Bypass -File tools\pc\esp.ps1 remove    (retire \EFI\Korin et \korin de la partition EFI)
#   ... esp.ps1 install -Esp ; ... esp.ps1 config -Esp -Default korin -Timeout 5 [-Cmdline "..."] ; config -Esp -Clear
#       (écrivent sur la partition EFI : seulement avec -Esp, pour qui le voudrait vraiment)
#   « default » et « undefault » : retirés d'ici, voir korin.ps1 (son « undefault » retire aussi les anciennes
#   entrées « Korin OS » qui menaient à la partition EFI).
#
# Fichiers de Korin sur la partition EFI (install -Esp, config -Esp ; rien d'autre) :
#   \EFI\Korin\korin.efi    le chargeur (build\pc\esp\EFI\BOOT\BOOTX64.EFI)
#   \korin\kernel.elf       le noyau
#   \korin\initrd.tar       les premiers programmes
#   \korin\boot.cfg         seulement par « config » (sans lui : menu, Windows par défaut, 5 s)
# Jamais touchés : \EFI\Microsoft, \EFI\Boot, le BCD de Windows (son entrée {bootmgr}).
# La partition est montée sous une lettre libre le temps du travail, puis démontée.
param(
    [Parameter(Mandatory = $true)][ValidateSet('install', 'remove', 'show', 'config', 'default', 'undefault')]
    [string]$Action,
    [ValidateSet('korin', 'windows')][string]$Default,
    [ValidateRange(0, 3600)][int]$Timeout = -1,
    [string]$Cmdline,
    [switch]$Clear,
    [switch]$Force,
    [switch]$Esp
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDir = Join-Path $root 'build\pc\esp'
$srcEfi = Join-Path $buildDir 'EFI\BOOT\BOOTX64.EFI'
$srcKernel = Join-Path $buildDir 'korin\kernel.elf'
$srcInitrd = Join-Path $buildDir 'korin\initrd.tar'
$korinPath = '\EFI\Korin\korin.efi'
$bootmgrId = '{9dea862c-5cdd-4e70-acc1-f32b344d4795}'    # {bootmgr} : Windows Boot Manager
$fwbootmgrId = '{a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}'  # {fwbootmgr} : l'ordre du firmware
$guidRx = '\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($Action -in 'default', 'undefault') {
    throw "« esp.ps1 $Action » n'existe plus : Korin démarre de la partition KORIN OS, voir « tools\pc\korin.ps1 $Action »"
}
if ($Action -in 'install', 'config' -and -not $Esp) {
    throw "« esp.ps1 $Action » écrirait sur la partition EFI de Windows : Korin va maintenant sur KORIN OS (« tools\pc\korin.ps1 $Action »). Pour écrire quand même sur la partition EFI : -Esp"
}
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
            Write-Host "(ANCIEN : Korin va maintenant sur KORIN OS, voir tools\pc\korin.ps1)"
        }
        'remove' {
            # les entrées « Korin OS » qui mènent à la partition EFI (sans lettre : partition=\Device\…) ;
            # celles de KORIN OS (partition=D:) ne gênent pas
            $k = @(Get-KorinEntries | Where-Object { $_.Text -notmatch 'partition=[A-Za-z]:' })
            if ($k.Count -gt 0 -and -not $Force) {
                throw "Le firmware a encore une entrée « Korin OS » vers la partition EFI ($($k.Id -join ', ')) : « korin.ps1 default » (la remet sur KORIN OS) ou « korin.ps1 undefault » d'abord (ou -Force)"
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
            if (-not (Test-Path $dKorin)) { throw "Korin n'est pas installé ($dKorin absent) : « esp.ps1 install -Esp » d'abord" }
            $lines = @('# Menu de démarrage de Korin OS (tools\pc\esp.ps1 config)')
            if ($Default) { $lines += "default = $Default" }
            if ($Timeout -ge 0) { $lines += "timeout = $Timeout" }
            if ($PSBoundParameters.ContainsKey('Cmdline')) { $lines += "cmdline = $Cmdline" }
            [IO.File]::WriteAllText($cfg, (($lines -join "`n") + "`n"), [Text.Encoding]::ASCII)
            Write-Host "\korin\boot.cfg :"
            $lines | ForEach-Object { Write-Host "  | $_" }
        }
    }
} finally {
    & mountvol $L /D | Out-Null
}
