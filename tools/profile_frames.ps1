param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if(-not $SkipBuild) {
    & "$repo/firmware/build.ps1" -Profile
    if($LASTEXITCODE){throw 'Profile firmware build failed'}
}
$sim=(Get-Command vsim.exe -ErrorAction Stop).Source
$runDir=Join-Path $repo 'tmp/frame_profile'
New-Item -ItemType Directory -Force $runDir | Out-Null
$sources=Get-ChildItem "$repo/rtl" -Filter '*.v' |
    Where-Object {$_.BaseName -notin @('top','top_audio','eg_adc','hdmi_game','hdmi_test')}
$quoted=@($sources | ForEach-Object {'{'+$_.FullName.Replace('\','/')+'}'})
$quoted+='{' + "$repo/tools/tb_frame_profile.v".Replace('\','/') + '}'
@('onerror {quit -code 1}','vlib work',('vlog +define+HDMI_SIM_RAM -work work '+($quoted -join ' ')),
  'vsim work.tb_frame_profile','run -all','quit -code 0') |
    Set-Content -Encoding ascii "$runDir/profile.do"
Push-Location $runDir
try {
    & $sim -c -do profile.do *> measured.log
    $result=Get-Content measured.log -Raw
    if($LASTEXITCODE -or $result -notmatch 'PASS profile:' -or $result -match '\*\* (Fatal|Error)') {
        throw "Frame profile failed; inspect $runDir/measured.log"
    }
    Select-String -Path measured.log -Pattern '^# (PROFILE |PASS profile:)' |
        ForEach-Object {$_.Line}
} finally {Pop-Location}
