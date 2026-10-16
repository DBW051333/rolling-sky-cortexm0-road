param([switch]$Simulation,[switch]$Benchmark,[switch]$Profile)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $bin='C:\Keil_v5\ARM\ARMCLANG\bin'
    New-Item -ItemType Directory -Path Output -Force | Out-Null
    $flags=@(); $image='code.hex'
    if($Simulation){$flags=@('-DROAD_SIM_FAST_INIT');$image='code.sim.hex'}
    if($Profile){$flags=@('-DROAD_SIM_FAST_INIT','-DROAD_PROFILE');$image='code.profile.hex'}
    if($Benchmark){$flags=@('-DROAD_SIM_FAST_INIT','-DROAD_BENCHMARK');$image='code.bench.hex'}
    foreach($file in @('main','road','level','walls','sd_levels','glass','appearance')) {
        $opt=if($file -in @('appearance','main')){'-Oz'}else{'-O2'}
        & "$bin\armclang.exe" --target=arm-arm-none-eabi -mcpu=cortex-m0 -mthumb $opt -ffreestanding -fno-builtin -fno-common -g -Wall -Wextra @flags -c "$file.c" -o "Output/$file.o"
        if($LASTEXITCODE){throw "Compile failed: $file"}
    }
    & "$bin\armasm.exe" --cpu Cortex-M0 startup.s -o Output/startup.o
    if($LASTEXITCODE){throw 'Startup failed'}
    $extraObjects=@()
    if($Benchmark) {
        foreach($file in @('road_before','wrapper')) {
            & "$bin\armclang.exe" --target=arm-arm-none-eabi -mcpu=cortex-m0 -mthumb -O2 -ffreestanding -fno-builtin -fno-common -g -Wall -Wextra @flags -c "../bench/$file.c" -o "Output/$file.o"
            if($LASTEXITCODE){throw "Benchmark compile failed: $file"}
            $extraObjects+="Output/$file.o"
        }
    }
    & "$bin\armlink.exe" --no_scanlib --scatter code.sct --entry Reset_Handler --map --list Output/code.map -o Output/code.axf Output/startup.o Output/main.o Output/road.o Output/level.o Output/walls.o Output/sd_levels.o Output/glass.o Output/appearance.o @extraObjects
    if($LASTEXITCODE){throw 'Link failed'}
    & "$bin\fromelf.exe" --vhx --32x1 Output/code.axf --output $image
    if($LASTEXITCODE){throw 'HEX failed'}
    & "$bin\fromelf.exe" --text -c Output/code.axf --output Output/code.disassembly.txt
    if($LASTEXITCODE){throw 'Disassembly failed'}
    Write-Host "Built Cortex-M0 road firmware: $image"
} finally { Pop-Location }
