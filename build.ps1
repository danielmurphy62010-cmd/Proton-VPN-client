$ErrorActionPreference = "Stop"

if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    throw "Rust toolchain not found. Install rustup and the stable MSVC toolchain."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake not found."
}

$QtRoot = $env:QT_ROOT
if (-not $QtRoot) { $QtRoot = $env:Qt6_DIR }
if (-not $QtRoot) { throw "Set QT_ROOT or Qt6_DIR to your Qt 6 installation prefix (with Widgets, Network and WebEngineWidgets)." }

Write-Host "Building Rust supervisor..."
cargo build --release

$BuildDir = Join-Path $PSScriptRoot "build/gui"
Write-Host "Building Qt 6 tray GUI..."
cmake -S (Join-Path $PSScriptRoot "gui") -B $BuildDir -DCMAKE_PREFIX_PATH="$QtRoot" -DCMAKE_BUILD_TYPE=Release
cmake --build $BuildDir --config Release

$GuiCandidates = @(
    (Join-Path $BuildDir "Release/proton-vpn-gui.exe"),
    (Join-Path $BuildDir "proton-vpn-gui.exe")
)
$GuiExe = $GuiCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $GuiExe) { throw "CMake completed but proton-vpn-gui.exe was not found in $BuildDir." }

$GuiOut = Join-Path $PSScriptRoot "target/release/proton-vpn-gui.exe"
Copy-Item $GuiExe $GuiOut -Force

# Deploy Qt runtime DLLs, plugins and the Qt WebEngine helper beside the GUI.
$QMake = Get-Command qmake -ErrorAction SilentlyContinue
if ($QMake) {
    $QtBin = Split-Path $QMake.Source -Parent
    $WinDeployQt = Join-Path $QtBin "windeployqt.exe"
    if (Test-Path $WinDeployQt) {
        & $WinDeployQt --release --compiler-runtime --no-translations $GuiOut
        if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
    } else {
        Write-Warning "windeployqt.exe not found; the GUI may need Qt DLLs and plugins on PATH."
    }
} else {
    Write-Warning "qmake was not found on PATH; skipped Qt runtime deployment."
}
Write-Host "Built target/release/proton-vpn-client.exe and deployed target/release/proton-vpn-gui.exe"
Write-Host "Place ./Tor/tor/tor.exe beside the project (or configure another path) before running."
