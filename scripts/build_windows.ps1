$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
cmake -S $root -B "$root/build-native" -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build "$root/build-native" --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir "$root/build-native" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Executable: $root/build-native/Release/desktop_snow.exe"
