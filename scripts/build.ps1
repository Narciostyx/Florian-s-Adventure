<#
.SYNOPSIS
    Build Florian's Adventure.

.DESCRIPTION
    Installs vcpkg dependencies (manifest mode) and builds the solution with MSBuild.

.PARAMETER Configuration
    Debug | Development | Release. Default: Development.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Configuration Release -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Development', 'Release')]
    [string]$Configuration = 'Development',

    [string]$Solution = 'FloriansAdventure.sln',

    [switch]$Clean,

    [switch]$Test,

    [switch]$NoInstall
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$triplet = 'x64-windows-static-md'

Push-Location $repoRoot
try {
    if (-not (Test-Path $Solution)) {
        throw "Solution '$Solution' not found. Run from a complete checkout (M00 must be applied first)."
    }

    if (-not $env:VCPKG_ROOT) {
        throw 'VCPKG_ROOT is not set. Point it at your vcpkg checkout (the folder containing vcpkg.exe).'
    }
    $vcpkgExe = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
    if (-not (Test-Path $vcpkgExe)) {
        throw "vcpkg.exe not found under VCPKG_ROOT ($env:VCPKG_ROOT). Run bootstrap-vcpkg.bat there first."
    }

    $manifest = Get-Content -Raw 'vcpkg.json'
    if ($manifest -match 'TODO-REPLACE-WITH-REAL-COMMIT-SHA') {
        throw 'vcpkg.json still has the placeholder builtin-baseline. Replace it with a real vcpkg commit SHA.'
    }

    if ($Clean -and (Test-Path 'build')) {
        Write-Host '==> Cleaning build/'
        Remove-Item 'build' -Recurse -Force
    }

    if (-not $NoInstall) {
        Write-Host "==> vcpkg install (manifest, $triplet)"
        & $vcpkgExe install --triplet $triplet --host-triplet x64-windows
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install failed with exit code $LASTEXITCODE."
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio with the Desktop C++ workload.'
    }

    $msbuild = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)
    if (-not $msbuild) {
        throw 'MSBuild.exe not found via vswhere.'
    }

    Write-Host "==> Building $Solution [$Configuration|x64]"
    $msbuildArgs = @(
        $Solution,
        '/m',
        '/nologo',
        '/v:minimal',
        "/p:Configuration=$Configuration",
        '/p:Platform=x64'
    )
    & $msbuild @msbuildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }

    if ($Test) {
        $testExe = Join-Path $repoRoot "build\bin\x64\$Configuration\fa_tests.exe"
        if (Test-Path $testExe) {
            Write-Host '==> Running unit tests'
            & $testExe
            if ($LASTEXITCODE -ne 0) {
                throw "Tests failed with exit code $LASTEXITCODE."
            }
        }
        else {
            Write-Warning "Test executable not found at $testExe"
        }
    }

    Write-Host '==> Done'
}
finally {
    Pop-Location
}
