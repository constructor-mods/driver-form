# Makes the two release files in release\: msyeett.driver-form-<version>.swoftyaddon and the same as .zip.
# Double-click package.bat in the repo (it builds the DLL first). GitHub runs this too when a version tag is pushed.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content (Join-Path $root 'swofty.json') -Raw | ConvertFrom-Json
$files = @('swofty.json', 'README.md', 'LICENSE', 'CHANGELOG.md', 'icons/driver-form.png', 'native/driver_form.dll')
foreach ($f in $files) {
    if (-not (Test-Path (Join-Path $root $f))) { throw "missing $f" }
}
$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force $out | Out-Null
$name = "$($manifest.id)-$($manifest.version)"
$addon = Join-Path $out "$name.swoftyaddon"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path $addon) { Remove-Item $addon }
$zip = [System.IO.Compression.ZipFile]::Open($addon, 'Create')
try {
    foreach ($f in $files) {
        # forward slashes inside the archive, so every tool reads the folders right
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $root $f), $f, 'Optimal')
    }
} finally {
    $zip.Dispose()
}
Copy-Item $addon (Join-Path $out "$name.zip") -Force
Get-ChildItem $out -Filter "$name.*" | ForEach-Object { "{0}  {1:N0} bytes" -f $_.Name, $_.Length }
