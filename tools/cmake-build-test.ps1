param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$BuildDir,

    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$CtestOptions
)

if ($BuildDir.StartsWith("-")) {
    Write-Host "Usage: cmake-build-test.ps1 <build_dir> [ctest options...]"
    exit 1
}

# Leak detection is off by default on macOS because of spurious error.
# This turns it on:
# $env:ASAN_OPTIONS = "detect_leaks=1"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Push-Location $BuildDir
try {
    cmake --build . --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # Copy tess.exe to project root
    $candidates = @(
        (Join-Path "src" "tess" "Release" "tess.exe"),
        (Join-Path "src" "tess" "tess.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            Copy-Item $candidate (Join-Path $projectRoot "tess.exe")
            Write-Host "Copied $candidate to $projectRoot"
            break
        }
    }

    ctest -C Release @CtestOptions
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
