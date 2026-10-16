$ErrorActionPreference='Stop'
& python "$PSScriptRoot\tools\make_appearance.py"
if($LASTEXITCODE){throw 'Appearance data generation failed'}
& "$PSScriptRoot\firmware\build.ps1"
$started=Get-Date
Push-Location "$PSScriptRoot\td"
try {
    'source build.tcl' | & 'C:\anlogic\TD_5.6.5_119222_SP7_198.377\bin\td_commands_prompt.exe' *> build.log
    if($LASTEXITCODE -or (Select-String -Path build.log -Pattern '\bERROR:' -Quiet)){throw 'TD failed; inspect td/build.log'}
    $image=Get-Item rolling_road.bit -ErrorAction Stop
    if($image.LastWriteTime -lt $started){throw 'No fresh bitstream generated; inspect td/build.log'}
    Write-Host "Generated: $($image.FullName)"
} finally { Pop-Location }
