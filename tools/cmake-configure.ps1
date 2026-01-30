param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$BuildType,

    [Parameter(Mandatory=$true, Position=1)]
    [string]$BuildDir,

    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$CmakeOptions
)

if ($BuildDir.StartsWith("-")) {
    Write-Host "Usage: cmake-configure.ps1 <build_type> <build_dir> [cmake options...]"
    Write-Host ""
    Write-Host "Example: .\tools\cmake-configure.ps1 Debug build-asan -DMOS_ASAN=YES"
    exit 1
}

$env:CMAKE_BUILD_TYPE = $BuildType

# Configure a shared CPM download cache
$env:CPM_SOURCE_CACHE = Join-Path (Get-Location) ".cache/CPM"
Write-Host "Using CPM cache: $env:CPM_SOURCE_CACHE"

if (Test-Path $BuildDir) {
    Write-Host "Deleting directory: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "Configuring with cmake into '$BuildDir'."
Write-Host ""

cmake @CmakeOptions -GNinja -B $BuildDir -S .

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Configuration into '$BuildDir' is complete. Build with 'cmake --build $BuildDir'."
} else {
    Write-Host ""
    Write-Host "Configuration failed."
}
