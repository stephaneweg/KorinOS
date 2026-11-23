# installkey.ps1 : écrire l'image de la clé d'installation de Korin OS sur une clé USB (docs/etude/INSTALLATEUR.md §2.1).
#
#   .\tools\pc\installkey.ps1 list                         les clés USB vues par Windows
#   .\tools\pc\installkey.ps1 write -Disk N [-Image FICHIER]
#
# L'image vient de tools/pc/installkey.py build (build/pc/installkey.img par défaut). Refusé : un disque qui n'est pas
# sur le bus USB, le disque de démarrage ou du système de Windows, un disque plus petit que l'image. Avant d'écrire,
# le numéro du disque est à retaper. Tout ce qui est sur la clé est effacé. À lancer en administrateur.
param(
    [Parameter(Position = 0)][ValidateSet('list', 'write')][string]$Action = 'list',
    [int]$Disk = -1,
    [string]$Image = ''
)
$ErrorActionPreference = 'Stop'
# (pas dans param() : Windows PowerShell 5.1 y laisse $PSScriptRoot vide quand le script est lancé par -File)
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Image) { $Image = Join-Path $here '..\..\build\pc\installkey.img' }

function Get-UsbDisks {
    Get-Disk | Where-Object { $_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem }
}

if ($Action -eq 'list') {
    $d = Get-UsbDisks
    if (-not $d) { Write-Host 'Aucune clé USB.'; exit 0 }
    $d | Format-Table Number, FriendlyName, @{n = 'Taille (Go)'; e = { [math]::Round($_.Size / 1GB, 1) } }, PartitionStyle -AutoSize
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Lancez PowerShell en administrateur (écrire un disque entier le demande).'
}
if (-not (Test-Path $Image)) { throw "Image absente : $Image (d'abord : python -I tools/pc/installkey.py build)" }
if ($Disk -lt 0) { throw 'Donnez le disque : -Disk N (voir : installkey.ps1 list)' }
$d = Get-Disk -Number $Disk
if ($d.BusType -ne 'USB') { throw "Le disque $Disk n'est pas sur le bus USB ($($d.BusType)) : refusé." }
if ($d.IsBoot -or $d.IsSystem) { throw "Le disque $Disk porte Windows : refusé." }
$img = Get-Item $Image
if ($img.Length -gt $d.Size) { throw "Clé trop petite : $([math]::Round($d.Size/1GB,1)) Go pour une image de $([math]::Round($img.Length/1GB,2)) Go." }

Write-Host ("Disque {0} : {1}, {2} Go. TOUT ce qu'il contient sera effacé." -f $Disk, $d.FriendlyName, [math]::Round($d.Size / 1GB, 1))
$answer = Read-Host 'Pour confirmer, retapez le numéro du disque'
if ($answer -ne "$Disk") { Write-Host 'Rien n''a été écrit.'; exit 1 }

# sans table, pour que Windows ne garde aucun volume ouvert pendant l'écriture (une clé neuve, jamais initialisée,
# n'a rien à effacer) ; hors ligne si Windows le permet (pas pour beaucoup de clés « amovibles »)
try { Clear-Disk -Number $Disk -RemoveData -RemoveOEM -Confirm:$false } catch { Write-Host "(Clear-Disk : $($_.Exception.Message))" }
$offline = $false
try { Set-Disk -Number $Disk -IsOffline $true; $offline = $true } catch { }

# les 4 premiers Mo (la table GPT) en dernier : Windows ne voit une partition qu'une fois tout le reste écrit
$src = [IO.File]::OpenRead($img.FullName)
$dst = New-Object IO.FileStream("\\.\PhysicalDrive$Disk", [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
try {
    $head = New-Object byte[] (4MB)
    $hn = $src.Read($head, 0, $head.Length)
    if ($hn % 512) { $hn += 512 - ($hn % 512) }
    $buf = New-Object byte[] (4MB)
    $done = $hn
    $dst.Seek($head.Length, [IO.SeekOrigin]::Begin) | Out-Null
    while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
        if ($n % 512) { [Array]::Clear($buf, $n, $buf.Length - $n); $n += 512 - ($n % 512) } # (la fin, complétée à un secteur)
        $dst.Write($buf, 0, $n)
        $done += $n
        Write-Progress -Activity 'Écriture de la clé' -Status ("{0} Mo sur {1} Mo" -f [int]($done / 1MB), [int]($img.Length / 1MB)) -PercentComplete ([math]::Min(100, $done * 100 / $img.Length))
    }
    $dst.Flush()
    $dst.Seek(0, [IO.SeekOrigin]::Begin) | Out-Null
    $dst.Write($head, 0, $hn)
    $dst.Flush()
} finally {
    $src.Close()
    $dst.Close()
}
if ($offline) { Set-Disk -Number $Disk -IsOffline $false }
Update-Disk -Number $Disk
Write-Host 'Clé écrite. Démarrez le PC dessus (F9 sur le ZBook, Secure Boot désactivé).'
