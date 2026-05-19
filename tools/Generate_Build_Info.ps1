param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectDir
)

$ErrorActionPreference = "Stop"

$date = Get-Date -Format "yyyyMMdd"
$timestamp = Get-Date -Format "yyyyMMddHHmmss"
$otaDir = Join-Path $ProjectDir "build\ota"
$srcDir = Join-Path $ProjectDir "src"
$header = Join-Path $srcDir "build_info.h"
$latestVersionFile = Join-Path $otaDir "latest_version.txt"
$counterFile = Join-Path $otaDir ("build_counter_{0}.txt" -f $date)

if (!(Test-Path $otaDir)) {
  New-Item -ItemType Directory -Path $otaDir | Out-Null
}

if (!(Test-Path $srcDir)) {
  New-Item -ItemType Directory -Path $srcDir | Out-Null
}

$number = 1
if (Test-Path $counterFile) {
  $raw = (Get-Content $counterFile -Raw).Trim()
  if ($raw -match '^\d+$') {
    $number = [int]$raw + 1
  }
}

Set-Content -Path $counterFile -Value $number -Encoding ASCII

$version = "{0}-{1:00}" -f $date, $number
Set-Content -Path $latestVersionFile -Value $version -Encoding ASCII

$content = @"
#pragma once

// Fichier genere automatiquement par tools/Generate_Build_Info.ps1.
// Ne pas modifier a la main si tu utilises les scripts de compilation.

#define ROUTEUR_FIRMWARE_VERSION "$version"
#define ROUTEUR_BUILD_DATE "$date"
#define ROUTEUR_BUILD_NUMBER $number
#define ROUTEUR_BUILD_TIMESTAMP "$timestamp"
#define ROUTEUR_FIRMWARE_MARKER "RS32_VERSION:$version;"

"@

Set-Content -Path $header -Value $content -Encoding ASCII
Write-Host $version
