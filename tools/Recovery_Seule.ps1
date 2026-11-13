param(
  [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port,
  [string]$RecoveryBin
)
$ErrorActionPreference = 'Stop'

function Assert-RecoveryImage([string]$Image) {
  $bytes = [IO.File]::ReadAllBytes($Image)
  if ($bytes.Length -lt 256 -or $bytes.Length -gt 0x110000) { throw 'Taille Recovery invalide (maximum 0x110000).' }
  if ($bytes[0] -ne 0xE9 -or $bytes[1] -lt 1 -or $bytes[1] -gt 16) { throw 'Image ESP invalide : choisir le .bin applicatif, pas une image fusionnee.' }
  if ([BitConverter]::ToUInt16($bytes, 12) -ne 0) { throw 'Image non destinee a ESP32 classique.' }
  if ([BitConverter]::ToUInt32($bytes, 32) -ne 0xABCD5432L) { throw 'Descripteur applicatif ESP32 absent.' }
  $text = [Text.Encoding]::ASCII.GetString($bytes)
  if (!$text.Contains('RSrecoverymode_') -or !$text.Contains('/upload-fs')) { throw 'Cette image ne correspond pas a la Recovery du projet.' }
}

function Assert-RecoveryPartitionTable([byte[]]$Table, [string]$Csv) {
  if ($Table.Length -ne 0x1000) { throw 'Lecture incomplete de la table de partitions.' }
  $expected = @{}
  foreach ($line in (Get-Content -LiteralPath $Csv)) {
    if (!$line.Trim() -or $line.Trim().StartsWith('#')) { continue }
    $c = @($line.Split(',') | ForEach-Object { $_.Trim() })
    $expected[$c[0]] = @($c[1], $c[2], [Convert]::ToUInt32($c[3].Substring(2),16), [Convert]::ToUInt32($c[4].Substring(2),16))
  }
  $types = @{ 'nvs' = @(1,2); 'otadata' = @(1,0); 'factory' = @(0,0); 'ota_0' = @(0,16); 'spiffs' = @(1,130) }
  $seen = @{}
  for ($offset = 0; $offset -lt $Table.Length; $offset += 32) {
    $magic = [BitConverter]::ToUInt16($Table,$offset)
    if ($magic -eq 0xFFFF -or $magic -eq 0xEBEB) { break }
    if ($magic -ne 0x50AA) { throw 'Table de partitions invalide.' }
    $name = [Text.Encoding]::ASCII.GetString($Table,$offset+12,16).TrimEnd([char]0)
    if (!$expected.ContainsKey($name) -or $seen.ContainsKey($name) -or !$types.ContainsKey($name)) { throw "Partition inattendue : $name" }
    $e = $expected[$name]; $t = $types[$name]
    if ($Table[$offset+2] -ne $t[0] -or $Table[$offset+3] -ne $t[1] -or
        [BitConverter]::ToUInt32($Table,$offset+4) -ne $e[2] -or
        [BitConverter]::ToUInt32($Table,$offset+8) -ne $e[3] -or
        [BitConverter]::ToUInt32($Table,$offset+28) -ne 0) { throw "Partition incompatible : $name" }
    $seen[$name] = $true
  }
  if ($seen.Count -ne 5 -or $expected.Count -ne 5 -or $expected['factory'][2] -ne 0x10000 -or $expected['factory'][3] -ne 0x110000) {
    throw 'Schema Recovery/Main 4 Mo requis. Aucune migration automatique.'
  }
}

function Invoke-RecoveryTool([string[]]$ToolArguments) {
  & $env:ESPTOOL @ToolArguments
  if ($LASTEXITCODE -ne 0) { throw "Echec esptool (code $LASTEXITCODE). Arret sans autre operation." }
}

function Update-RecoveryOnly([string]$SelectedPort, [string]$Image, [string]$Project) {
  Assert-RecoveryImage $Image
  $Image = (Resolve-Path -LiteralPath $Image).Path
  $imageHash = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash
  Write-Host "Recovery choisie : $Image"
  Write-Host "SHA-256 : $imageHash"
  Write-Host 'Seule la partition factory sera remplacee. Bootloader, table, NVS, otadata, Main et LittleFS ne sont pas ecrits.'
  Write-Host 'ESP32 classique 4 Mo ; fermer le moniteur serie. Sorties de puissance deconnectees.'
  if ((Read-Host "Pour remplacer uniquement la Recovery, saisir RECOVERY $SelectedPort") -cne "RECOVERY $SelectedPort") {
    throw 'Operation annulee avant tout acces au port serie.'
  }
  $backup = Join-Path $Project ('build\backups\recovery_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '_' + $SelectedPort)
  New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null
  $tableFile = Join-Path $backup 'partitions.bin'
  $oldImage = Join-Path $backup 'factory_avant.bin'
  $baseArgs = @('--chip','esp32','--port',$SelectedPort,'--baud','115200','--before','default_reset','--after','no_reset')
  Invoke-RecoveryTool ($baseArgs + @('read_flash','0x8000','0x1000',$tableFile))
  Assert-RecoveryPartitionTable ([IO.File]::ReadAllBytes($tableFile)) (Join-Path $Project 'partitions_recovery_main_4MB.csv')
  Invoke-RecoveryTool ($baseArgs + @('read_flash','0x10000','0x110000',$oldImage))
  if ((Get-Item -LiteralPath $oldImage).Length -ne 0x110000) { throw 'Sauvegarde factory incomplete : aucune ecriture.' }
  # Figer une copie verifiee pour eviter une modification du fichier choisi pendant les lectures.
  $stagedImage = Join-Path $backup 'recovery_a_installer.bin'
  Copy-Item -LiteralPath $Image -Destination $stagedImage -ErrorAction Stop
  if ((Get-FileHash -LiteralPath $stagedImage -Algorithm SHA256).Hash -ne $imageHash) { throw 'Image modifiee depuis sa selection.' }
  @{ port=$SelectedPort; offset='0x10000'; image=$Image; sha256=$imageHash; backupSha256=(Get-FileHash -LiteralPath $oldImage -Algorithm SHA256).Hash } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'operation.json') -Encoding ASCII
  Invoke-RecoveryTool ($baseArgs + @('write_flash','-z','0x10000',$stagedImage))
  $verifyArgs = @('--chip','esp32','--port',$SelectedPort,'--baud','115200','--before','default_reset','--after','hard_reset')
  Invoke-RecoveryTool ($verifyArgs + @('verify_flash','0x10000',$stagedImage))
  Write-Host "Recovery remplacee et verifiee. Sauvegarde : $backup"
  Write-Host 'La selection de boot est conservee. Si le Main etait selectionne, il redemarre ; demander explicitement la recovery depuis la WebUI pour la tester.'
}

try {
  if (!$Port) { $Port = (Read-Host 'Port ESP32 (exemple COM4)').Trim().ToUpperInvariant() }
  if ($Port -notmatch '^COM[1-9][0-9]*$') { throw 'Port COM invalide.' }
  if (!$RecoveryBin) { $RecoveryBin = (Read-Host 'Chemin du .bin Recovery applicatif a installer (pas de compilation automatique)').Trim().Trim('"') }
  if (!(Test-Path -LiteralPath $env:ESPTOOL -PathType Leaf)) { throw 'esptool introuvable.' }
  Update-RecoveryOnly $Port $RecoveryBin (Split-Path $PSScriptRoot -Parent)
  exit 0
} catch {
  Write-Host "ERREUR : $_" -ForegroundColor Red
  exit 1
}
