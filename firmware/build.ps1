param([switch]$Simulation,[switch]$Benchmark,[switch]$Profile)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $bin=$env:ARMCLANG_BIN
    if(-not $bin){$bin='C:\Keil_v5\ARM\ARMCLANG\bin'}
    if(-not (Test-Path -LiteralPath (Join-Path $bin 'armclang.exe') -PathType Leaf)){throw "Set ARMCLANG_BIN to the folder containing armclang.exe; not found: $bin"}
    New-Item -ItemType Directory -Path Output -Force | Out-Null
    $flags=@('-DMENU_VIDEO'); $image='code.hex'
    if($Simulation){$image='code.sim.hex'}
    if($Profile){$flags=@('-DMENU_VIDEO','-DROAD_PROFILE');$image='code.profile.hex'}
    if($Benchmark){$flags=@('-DMENU_VIDEO','-DROAD_BENCHMARK');$image='code.bench.hex'}
    foreach($file in @('main','road','menu','flyover','player','level','obstacle','walls','sd_levels','glass','appearance','sound','menu_video')) {
        # Keep the gameplay scanline renderer at -O2; compact helpers and the
        # flyover menu use -Oz within 31 KiB; the last 1 KiB is SD art cache.
        $opt=if($file -in @('menu','flyover','player','appearance','main','sd_levels','walls','level','glass','sound','obstacle','menu_video')){'-Oz'}else{'-O2'}
        # AC 6.24 otherwise expands the road renderer substantially.
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
    & "$bin\armlink.exe" --no_scanlib --scatter code.sct --entry Reset_Handler --map --list Output/code.map --callgraph --callgraph_file Output/code.callgraph.html -o Output/code.axf Output/startup.o Output/main.o Output/road.o Output/menu.o Output/flyover.o Output/player.o Output/level.o Output/obstacle.o Output/walls.o Output/sd_levels.o Output/glass.o Output/appearance.o Output/sound.o Output/menu_video.o @extraObjects
    if($LASTEXITCODE){throw 'Link failed'}
    & python "$PSScriptRoot/../tools/check_memory_layout.py"
    if($LASTEXITCODE){throw 'Code overlaps the writable SD artwork cache'}
    & "$bin\fromelf.exe" --vhx --32x1 Output/code.axf --output $image
    if($LASTEXITCODE){throw 'HEX failed'}
    & "$bin\fromelf.exe" --text -c Output/code.axf --output Output/code.disassembly.txt
    if($LASTEXITCODE){throw 'Disassembly failed'}
    Write-Host "Built Cortex-M0 road firmware: $image"
} finally { Pop-Location }
