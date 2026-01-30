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

Push-Location $BuildDir
try {
    cmake --build .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    ctest @CtestOptions
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
