# Copie les tables ACPI de la machine Windows où il tourne (DSDT, SSDT, FADT, RSDT : ce que Windows garde
# dans le registre, HKLM\HARDWARE\ACPI) en fichiers .dat, pour les essayer dans acpiexec
# (tools/acpica/acpiexec.sh). Celles du ZBook Fury 17 G8 sont dans tools/acpica/zbook (17/11/2026).
# Lecture seule : rien n'est écrit hors du dossier de sortie. Dans PowerShell :
#     powershell -File tools/acpica/dump_tables.ps1 [-Out build/pc/acpi-tables]
param([string]$Out = "build/pc/acpi-tables")

New-Item -ItemType Directory -Force $Out | Out-Null
$n = 0
foreach ($t in Get-ChildItem 'HKLM:\HARDWARE\ACPI') {
	# la table est la valeur « 00000000 » de la dernière sous-clé (OEM, modèle, révision)
	$k = Get-ChildItem $t.PSPath -Recurse | Where-Object { $_.Property -contains '00000000' } | Select-Object -First 1
	if (-not $k) { continue }
	$b = (Get-ItemProperty $k.PSPath).'00000000'
	[IO.File]::WriteAllBytes((Join-Path (Resolve-Path $Out) ($t.PSChildName + '.dat')), [byte[]]$b)
	"{0,-6} {1,8} octets" -f $t.PSChildName, $b.Length
	$n++
}
"$n table(s) dans $Out"
