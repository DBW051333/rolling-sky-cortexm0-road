param([switch]$Simulation,[switch]$Benchmark,[switch]$Profile)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $bin=$env:ARMCLANG_BIN
    if(-not $bin){$bin='C:\Keil_v5\ARM\ARMCLANG\bin'}
    if(-not (Test-Path -LiteralPath (Join-Path $bin 'armclang.exe') -PathType Leaf)){throw "Set ARMCLANG_BIN to the folder containing armclang.exe; not found: $bin"}
    New-Item -ItemType Directory -Path Output -Force | Out-Null
    $flags=@(); $image='code.hex'
    if($Simulation){$image='code.sim.hex'}
    if($Profile){$flags=@('-DROAD_PROFILE');$image='code.profile.hex'}
    if($Benchmark){$flags=@('-DROAD_BENCHMARK');$image='code.bench.hex'}
    foreach($file in @('main','road','menu','player','level','obstacle','walls','sd_levels','glass','appearance','sound')) {
        # level.c, glass.c, sound.c and obstacle.c are on the -Oz list to stay
        # under the 0x7000 ceiling now that the synth is back in the image.
        # Keep the gameplay scanline renderer and animated menu at -O2.
        # Smaller helpers retain -Oz to fit the 28 KiB code region.
        $opt=if($file -in @('player','appearance','main','sd_levels','walls','level','glass','sound','obstacle')){'-Oz'}else{'-O2'}
        # AC 6.24 unrolls road.c enough to overflow the 28 KiB code region.
        # Retain -O2 for rendering, but keep its loops compact on Cortex-M0.
        $loopFlags=@()
        if($file -in @('road','menu')){$loopFlags=@('-fno-unroll-loops')}
        & "$bin\armclang.exe" --target=arm-arm-none-eabi -mcpu=cortex-m0 -mthumb $opt @loopFlags -ffreestanding -fno-builtin -fno-common -g -Wall -Wextra @flags -c "$file.c" -o "Output/$file.o"
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
    & "$bin\armlink.exe" --no_scanlib --scatter code.sct --entry Reset_Handler --map --list Output/code.map --callgraph --callgraph_file Output/code.callgraph.html -o Output/code.axf Output/startup.o Output/main.o Output/road.o Output/menu.o Output/player.o Output/level.o Output/obstacle.o Output/walls.o Output/sd_levels.o Output/glass.o Output/appearance.o Output/sound.o @extraObjects
    if($LASTEXITCODE){throw 'Link failed'}
    & "$bin\fromelf.exe" --vhx --32x1 Output/code.axf --output $image
    if($LASTEXITCODE){throw 'HEX failed'}
    & "$bin\fromelf.exe" --text -c Output/code.axf --output Output/code.disassembly.txt
    if($LASTEXITCODE){throw 'Disassembly failed'}
    Write-Host "Built Cortex-M0 road firmware: $image"
} finally { Pop-Location }
