param([switch]$SkipFpga)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $bin=$env:ARMCLANG_BIN
    if(-not $bin){$bin='C:\Keil_v5\ARM\ARMCLANG\bin'}
    if(-not (Test-Path -LiteralPath (Join-Path $bin 'armclang.exe') -PathType Leaf)){throw "Set ARMCLANG_BIN to the folder containing armclang.exe"}
    New-Item -ItemType Directory -Path Output -Force | Out-Null
    & "$bin\armclang.exe" --target=arm-arm-none-eabi -mcpu=cortex-m0 -mthumb -Oz -ffreestanding -fno-builtin -fno-common -g -Wall -Wextra -c audio_test.c -o Output/audio_test.o
    if($LASTEXITCODE){throw 'Audio test compile failed'}
    & "$bin\armasm.exe" --cpu Cortex-M0 startup.s -o Output/startup.o
    if($LASTEXITCODE){throw 'Audio test startup failed'}
    & "$bin\armlink.exe" --no_scanlib --scatter code.sct --entry Reset_Handler --map --list Output/audio_code.map -o Output/audio_code.axf Output/startup.o Output/audio_test.o
    if($LASTEXITCODE){throw 'Audio test link failed'}
    & "$bin\fromelf.exe" --vhx --32x1 Output/audio_code.axf --output audio_code.hex
    if($LASTEXITCODE){throw 'Audio test HEX failed'}
    if($SkipFpga){Write-Host 'Built audio test firmware only';return}
} finally { Pop-Location }
