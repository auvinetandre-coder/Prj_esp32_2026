# Tests hors materiel : fonctions de production chargees sans executer le point d'entree.
$ErrorActionPreference = 'Stop'
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '..\Recovery_Seule.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw $errors[0] }
$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | ForEach-Object { . ([scriptblock]::Create($_.Extent.Text)) }
$testRoot=Join-Path $project ('build\tests\recovery_seule_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'partitions_recovery_main_4MB.csv') -Destination $testRoot
$image=Join-Path $testRoot 'recovery.bin'
$bytes=New-Object byte[] 512
$bytes[0]=0xE9; $bytes[1]=1
[BitConverter]::GetBytes([uint32]2882360370).CopyTo($bytes,32)
[Text.Encoding]::ASCII.GetBytes('RSrecoverymode_ /upload-fs').CopyTo($bytes,100)
[IO.File]::WriteAllBytes($image,$bytes)
$table=New-Object byte[] 4096
for($i=0;$i -lt $table.Length;$i++){ $table[$i]=255 }
$entries=@(@('nvs',1,2,0x9000,0x5000),@('otadata',1,0,0xE000,0x2000),@('factory',0,0,0x10000,0x110000),@('ota_0',0,16,0x120000,0x1E0000),@('spiffs',1,130,0x300000,0x100000))
$i=0
foreach($e in $entries){
 $off=$i*32; for($j=0;$j -lt 32;$j++){ $table[$off+$j]=0 }
 $table[$off]=0xAA; $table[$off+1]=0x50; $table[$off+2]=$e[1]; $table[$off+3]=$e[2]
 [BitConverter]::GetBytes([uint32]$e[3]).CopyTo($table,$off+4)
 [BitConverter]::GetBytes([uint32]$e[4]).CopyTo($table,$off+8)
 [Text.Encoding]::ASCII.GetBytes($e[0]).CopyTo($table,$off+12);$i++
}
$script:table=$table; $script:calls=@(); $script:mode='ok'; $script:confirm=$true
function Read-Host([string]$Prompt) { if($script:confirm){return 'RECOVERY COM4'};return 'NON' }
function Invoke-RecoveryTool([string[]]$ToolArguments) {
 $script:calls+=,@($ToolArguments)
 $command=($ToolArguments | Where-Object {$_ -in 'read_flash','write_flash','verify_flash'})
 $dest=$ToolArguments[-1]
 if($command -eq 'read_flash'){
  if($ToolArguments[-3] -eq '0x8000'){
   $copy=$script:table.Clone();if($script:mode -eq 'badtable'){$copy[70]=2}
   [IO.File]::WriteAllBytes($dest,$copy)
  } else {
   $size=0x110000;if($script:mode -eq 'shortbackup'){$size=32}
   [IO.File]::WriteAllBytes($dest,(New-Object byte[] $size))
  }
 }
 if($command -eq 'write_flash' -and $script:mode -eq 'writefail'){throw 'Echec ecriture simule'}
}
function Expect-Failure([scriptblock]$Action){$failed=$false;try{& $Action}catch{$failed=$true};if(!$failed){throw 'Refus attendu'} }
Assert-RecoveryImage $image
Assert-RecoveryPartitionTable $table (Join-Path $testRoot 'partitions_recovery_main_4MB.csv')
$invalid=$bytes.Clone();$invalid[12]=9;[IO.File]::WriteAllBytes((Join-Path $testRoot 'wrongchip.bin'),$invalid)
Expect-Failure { Assert-RecoveryImage (Join-Path $testRoot 'wrongchip.bin') }
[IO.File]::WriteAllBytes((Join-Path $testRoot 'empty.bin'),[byte[]]@())
Expect-Failure { Assert-RecoveryImage (Join-Path $testRoot 'empty.bin') }
Update-RecoveryOnly 'COM4' $image $testRoot
if($script:calls.Count -ne 4){throw 'Quatre appels attendus'}
$writes=@($script:calls | Where-Object {$_ -contains 'write_flash'})
if($writes.Count -ne 1 -or $writes[0][-2] -ne '0x10000'){throw 'Ecriture hors factory'}
foreach($call in $script:calls){if($call -contains 'erase_flash'){throw 'Effacement interdit'} }
foreach($mode in 'badtable','shortbackup','writefail'){
 $script:mode=$mode;$script:calls=@()
 Expect-Failure { Update-RecoveryOnly 'COM4' $image $testRoot }
 if($mode -ne 'writefail' -and @($script:calls | Where-Object {$_ -contains 'write_flash'}).Count){throw 'Ecriture apres refus'}
 if(@($script:calls | Where-Object {$_ -contains 'verify_flash'}).Count){throw 'Verification apres echec'}
}
$script:confirm=$false;$script:calls=@()
Expect-Failure { Update-RecoveryOnly 'COM4' $image $testRoot }
if($script:calls.Count){throw 'Acces serie apres annulation'}
Write-Host 'PASS : image/puce, table, sauvegarde, factory seule, verification, refus et annulation. Aucun acces materiel.'

