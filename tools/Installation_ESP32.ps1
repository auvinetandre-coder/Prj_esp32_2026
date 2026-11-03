param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('Effacer', 'Recovery')][string]$Phase,
  [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port
)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
  & $Program @Arguments
  if ($LASTEXITCODE -ne 0) { throw "Echec de $Program (code $LASTEXITCODE). Arret de la phase." }
}

try {
  if (!$Port) {
    Write-Host ('Ports presents : ' + ([IO.Ports.SerialPort]::GetPortNames() -join ', '))
    $Port = (Read-Host 'Port du nouvel ESP32 (exemple COM4)').Trim().ToUpperInvariant()
  }
  if ($Port -notmatch '^COM[1-9][0-9]*$') { throw 'Port COM invalide.' }
  if (!(Test-Path -LiteralPath $env:ESPTOOL)) { throw 'esptool introuvable.' }
  Write-Host "ESP32 classique 4 Mo uniquement. Fermer le moniteur serie sur $Port."
  Write-Host 'Effectuer cette installation sur banc, sorties de puissance deconnectees.'

  if ($Phase -eq 'Effacer') {
    Write-Host 'OPTIONNEL : effacement TOTAL, recovery, application, WiFi et /config compris.'
    if ((Read-Host "Pour une remise a zero volontaire, saisir EFFACER $Port") -cne "EFFACER $Port") {
      throw 'Effacement annule. Aucune ecriture effectuee.'
    }
    Invoke-Checked $env:ESPTOOL @('--chip','esp32','--port',$Port,'--baud','115200','erase_flash')
    Write-Host 'Flash effacee. Executer maintenant 02_Installer_Recovery.bat.'
    exit 0
  }

  if (!(Test-Path -LiteralPath $env:ARDUINO_CLI)) { throw 'arduino-cli introuvable.' }
  $build = Join-Path $project 'build\recovery'
  # Une copie locale du sketch permet de fournir partitions.csv sans modifier le core Arduino.
  $sketch = Join-Path $project 'build\installation\RouteurSolaireRecovery'
  New-Item -ItemType Directory -Path $sketch -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $project 'recovery\RouteurSolaireRecovery\RouteurSolaireRecovery.ino') -Destination $sketch
  Copy-Item -LiteralPath (Join-Path $project 'partitions_recovery_main_4MB.csv') -Destination (Join-Path $sketch 'partitions.csv')
  Invoke-Checked $env:ARDUINO_CLI @('compile','--fqbn','esp32:esp32:esp32:PartitionScheme=custom',
    '--build-path',$build,'--build-property','upload.maximum_size=1114112',$sketch)

  $boot = Join-Path $build 'RouteurSolaireRecovery.ino.bootloader.bin'
  $table = Join-Path $build 'RouteurSolaireRecovery.ino.partitions.bin'
  $recovery = Join-Path $build 'RouteurSolaireRecovery.ino.bin'
  foreach ($file in @($boot,$table,$recovery)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Binaire absent : $file" }
  }
  if ((Get-Item -LiteralPath $boot).Length -gt 0x7000) { throw 'Bootloader trop grand.' }
  if ((Get-Item -LiteralPath $table).Length -gt 0x1000) { throw 'Table trop grande.' }
  if ((Get-Item -LiteralPath $recovery).Length -gt 0x110000) { throw 'Recovery trop grande.' }
  if ((Get-FileHash (Join-Path $build 'partitions.csv')).Hash -ne
      (Get-FileHash (Join-Path $project 'partitions_recovery_main_4MB.csv')).Hash) {
    throw 'Le partitionnement compile ne correspond pas au CSV du projet.'
  }
  # otadata vierge selectionne factory au prochain boot, meme si un ancien OTA etait selectionne.
  $ota = Join-Path $build 'otadata_factory.bin'
  [IO.File]::WriteAllBytes($ota, [byte[]](,(0xFF) * 0x2000))
  Write-Host 'Installation initiale : bootloader 0x1000, table 0x8000, otadata 0xE000, recovery 0x10000.'
  Write-Host 'Reserve a une carte neuve/effacee : le changement de table peut rendre une ancienne configuration inaccessible.'
  Write-Host 'Pour un ESP configure, utiliser la recovery existante et conserver /config.'
  if ((Read-Host "Pour installer sur la carte neuve, saisir INSTALLER $Port") -cne "INSTALLER $Port") {
    throw 'Installation annulee. Binaires compiles, aucune ecriture sur ESP.'
  }
  Invoke-Checked $env:ESPTOOL @('--chip','esp32','--port',$Port,'--baud','115200',
    '--before','default_reset','--after','hard_reset','write_flash','-z',
    '0x1000',$boot,'0x8000',$table,'0xE000',$ota,'0x10000',$recovery)
  Write-Host 'Recovery installee. Verifier les logs a 115200 bauds et le reseau RSrecoverymode_XXXX.'
  Write-Host 'Page : http://192.168.4.1/recovery. Suite : phases 03 puis 04.'
} catch {
  Write-Host "ERREUR : $_" -ForegroundColor Red
  exit 1
}
