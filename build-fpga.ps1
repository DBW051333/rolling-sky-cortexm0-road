$ErrorActionPreference='Stop'
& python "$PSScriptRoot\tools\make_players.py"
if($LASTEXITCODE){throw 'Player art generation failed'}
& python "$PSScriptRoot\tools\make_appearance.py"
if($LASTEXITCODE){throw 'Appearance data generation failed'}
& "$PSScriptRoot\firmware\build.ps1"
if($LASTEXITCODE){throw 'Firmware build failed'}
$started=Get-Date
Push-Location "$PSScriptRoot\td"
try {
    $tdPrompt=$env:TD_COMMANDS_PROMPT
    if(-not $tdPrompt){$tdPrompt='C:\anlogic\TD_5.6.5_119222_SP7_198.377\bin\td_commands_prompt.exe'}
    if(-not (Test-Path -LiteralPath $tdPrompt -PathType Leaf)){throw "Set TD_COMMANDS_PROMPT to the full path of td_commands_prompt.exe; not found: $tdPrompt"}
    'source build.tcl' | & $tdPrompt *> build.log
    if($LASTEXITCODE -or (Select-String -Path build.log -Pattern '\bERROR:' -Quiet)){throw 'TD failed; inspect td/build.log'}
    $image=Get-Item rolling_road.bit -ErrorAction Stop
    if($image.LastWriteTime -lt $started){throw 'No fresh bitstream generated; inspect td/build.log'}
    $timing=Get-Content rolling_road.timing -Raw -ErrorAction Stop
    $summary=[regex]::Match($timing,'Timing violations:\s*(\d+) setup errors, and (\d+) hold errors')
    if(-not $summary.Success){throw 'No timing summary; inspect td/rolling_road.timing'}
    if([int]$summary.Groups[1].Value -or [int]$summary.Groups[2].Value){
        throw "Timing failed: $($summary.Value); do not download this image"
    }
    Write-Host "Generated: $($image.FullName)"
} finally { Pop-Location }
