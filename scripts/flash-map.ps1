# Writes a roads.bin into the device's map partition (see esp32/SYSTEM.md).
#   powershell -ExecutionPolicy Bypass -File scripts\flash-map.ps1                       # uses roads.bin in the project
#   powershell -ExecutionPolicy Bypass -File scripts\flash-map.ps1 C:\Users\me\Downloads\roads.bin
param([string]$Roads = (Join-Path $PSScriptRoot '..\roads.bin'), [string]$Port = 'COM7')
$ErrorActionPreference = 'Stop'
$Roads = (Resolve-Path $Roads).Path
$tools = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools'
$mk = (Get-ChildItem $tools -Recurse -Filter mklittlefs.exe | Select-Object -First 1).FullName
$es = (Get-ChildItem (Join-Path $tools 'esptool_py') -Recurse -Filter esptool.exe | Select-Object -First 1).FullName
$tmp = Join-Path $env:TEMP 'speed-limit-map'
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $tmp 'data') | Out-Null
Copy-Item $Roads (Join-Path $tmp 'data\roads.bin')
& $mk -c (Join-Path $tmp 'data') -p 256 -b 4096 -s 6291456 (Join-Path $tmp 'map.bin')
if ($LASTEXITCODE) { throw 'mklittlefs failed' }
& $es --chip esp32s3 -p $Port -b 921600 write-flash 0x210000 (Join-Path $tmp 'map.bin')
